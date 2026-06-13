#include "collabsession.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

CollabSession::CollabSession(int siteId, Role role, QObject *parent)
    : QObject(parent)
    , socket(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
    , siteId_(siteId)
    , role_(role)
{
    connect(socket, &QWebSocket::connected, this, &CollabSession::onConnected);
    connect(socket, &QWebSocket::disconnected, this, &CollabSession::onDisconnected);
    connect(socket, &QWebSocket::textMessageReceived, this, &CollabSession::onRawMessage);
    connect(socket,
            QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
            this,
            &CollabSession::onError);
}

CollabSession::~CollabSession()
{
    disconnectFromServer();
    for (auto *mgr : active)
        mgr->deleteLater();
    active.clear();
}

void CollabSession::connectToServer(const QString &url)
{
    socket->open(QUrl(url));
}
void CollabSession::disconnectFromServer()
{
    socket->close();
}

bool CollabSession::isConnected() const
{
    return socket->state() == QAbstractSocket::ConnectedState;
}

void CollabSession::setProject(const QString &projectRoot, const QStringList &relFiles)
{
    projectRoot_ = projectRoot;
    fileList_ = relFiles;
}

void CollabSession::initFileCache(const QString &relPath, const QString &text)
{
    textCache[relPath] = text;
}

void CollabSession::sendAllSnapshots(const QMap<QString, QString> &fileTexts)
{
    for (auto it = fileTexts.cbegin(); it != fileTexts.cend(); ++it) {
        const QString &relPath = it.key();
        const QString &text = it.value();

        textCache[relPath] = text;

        QJsonObject msg;
        msg["type"] = "snapshot";
        msg["file"] = relPath;
        msg["text"] = text;
        sendMessage(msg);
    }
}

void CollabSession::broadcastRunOutput(const QString &text)
{
    QJsonObject msg;
    msg["type"] = "run_output";
    msg["text"] = text;
    sendMessage(msg);
}

void CollabSession::notifyFileCreated(const QString &relPath, const QString &text)
{
    if (!fileList_.contains(relPath))
        fileList_.append(relPath);
    textCache[relPath] = text;

    QJsonObject msg;
    msg["type"] = "file_create";
    msg["file"] = relPath;
    msg["text"] = text;
    sendMessage(msg);
}

void CollabSession::notifyFileDeleted(const QString &relPath)
{
    fileList_.removeAll(relPath);
    textCache.remove(relPath);
    fileStates.remove(relPath);
    if (auto *mgr = active.take(relPath)) {
        lastUsed.remove(relPath);
        mgr->deleteLater();
    }

    QJsonObject msg;
    msg["type"] = "file_delete";
    msg["file"] = relPath;
    sendMessage(msg);
}

void CollabSession::sendCursorLeave(const QString &relPath)
{
    QJsonObject msg;
    msg["type"] = "cursor_leave";
    msg["file"] = relPath;
    msg["siteId"] = siteId_;
    sendMessage(msg);
}

void CollabSession::sendFileFocus(const QString &relPath)
{
    QJsonObject msg;
    msg["type"] = "file_focus";
    msg["file"] = relPath;
    msg["siteId"] = siteId_;
    sendMessage(msg);
}

void CollabSession::notifyFileRenamed(const QString &oldPath, const QString &newPath)
{
    const int idx = fileList_.indexOf(oldPath);
    if (idx >= 0)
        fileList_[idx] = newPath;

    if (textCache.contains(oldPath))
        textCache[newPath] = textCache.take(oldPath);
    if (fileStates.contains(oldPath))
        fileStates[newPath] = fileStates.take(oldPath);
    if (auto *mgr = active.take(oldPath)) {
        mgr->setFilePath(newPath);
        active[newPath] = mgr;
        lastUsed[newPath] = lastUsed.take(oldPath);
    }

    QJsonObject msg;
    msg["type"] = "file_rename";
    msg["old"] = oldPath;
    msg["new"] = newPath;
    sendMessage(msg);
}

RGAManager *CollabSession::getOrCreateRGA(const QString &relPath)
{
    if (auto *mgr = active.value(relPath, nullptr)) {
        lastUsed[relPath] = QDateTime::currentDateTime();
        return mgr;
    }

    if (active.size() >= MAX_ACTIVE)
        evictLRU();

    auto *mgr = new RGAManager(siteId_, this);
    mgr->setFilePath(relPath);
    mgr->setSendFunction([this](QJsonObject msg) { sendMessage(msg); });

    const FileState &state = fileStates.value(relPath);
    if (!state.sequence.rgaseq.isEmpty()) {
        mgr->setSequence(state.sequence);
        mgr->setTimestamp(state.maxTimestamp);
    } else if (textCache.contains(relPath)) {
        mgr->buildFromText(textCache[relPath]);
        fileStates[relPath].sequence = mgr->getSequence();
        fileStates[relPath].maxTimestamp = mgr->getTimestamp();
    }

    active[relPath] = mgr;
    lastUsed[relPath] = QDateTime::currentDateTime();
    return mgr;
}

void CollabSession::releaseRGA(const QString &relPath)
{
    auto *mgr = active.take(relPath);
    if (!mgr)
        return;

    auto &state = fileStates[relPath];
    state.sequence = mgr->getSequence();
    state.maxTimestamp = mgr->getTimestamp();
    textCache[relPath] = mgr->getText();
    lastUsed.remove(relPath);
    mgr->deleteLater();
}

void CollabSession::evictLRU()
{
    if (active.isEmpty())
        return;

    QString oldest;
    QDateTime oldestTime = QDateTime::currentDateTime().addSecs(1);

    for (auto it = lastUsed.cbegin(); it != lastUsed.cend(); ++it) {
        if (active.contains(it.key()) && it.value() < oldestTime) {
            oldestTime = it.value();
            oldest = it.key();
        }
    }
    if (oldest.isEmpty())
        oldest = active.firstKey();

    auto *mgr = active.take(oldest);
    auto &state = fileStates[oldest];
    state.sequence = mgr->getSequence();
    state.maxTimestamp = mgr->getTimestamp();
    textCache[oldest] = mgr->getText();
    lastUsed.remove(oldest);
    mgr->deleteLater();
}

void CollabSession::applyOpToInactive(const QString &file, const QJsonObject &op)
{
    auto &state = fileStates[file];

    if (state.sequence.rgaseq.isEmpty() && textCache.contains(file)) {
        const QString text = textCache[file];
        int ts = 0;
        RGAId parent{};
        state.sequence.rgaseq.reserve(text.size());
        for (QChar c : text) {
            ++ts;
            RGANode node;
            node.id = {ts, 0};
            node.parent = parent;
            node.val = c;
            node.tombstone = false;
            state.sequence.rgaseq.append(node);
            parent = node.id;
        }
        state.maxTimestamp = ts;
        state.sequence.rebuildIndex();
    }

    const QString type = op["type"].toString();

    if (type == "insert") {
        RGANode node = RGAManager::nodeFromJson(op["node"].toObject());
        state.maxTimestamp = qMax(state.maxTimestamp, node.id.timestamp) + 1;
        state.sequence.insert(node);
        textCache[file] = state.sequence.toText();
    } else if (type == "delete") {
        RGAId id = RGAManager::idFromJson(op["id"].toObject());
        state.sequence.remove(id);
        textCache[file] = state.sequence.toText();
    } else if (type == "undelete") {
        RGAId id = RGAManager::idFromJson(op["id"].toObject());
        state.sequence.undelete(id);
        textCache[file] = state.sequence.toText();
    }
}

void CollabSession::onConnected()
{
    sendRegister();
    emit connected();
}

void CollabSession::onDisconnected()
{
    emit disconnected();
}

void CollabSession::onRawMessage(const QString &message)
{
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (doc.isObject())
        handleMessage(doc.object());
}

void CollabSession::onError(QAbstractSocket::SocketError)
{
    emit errorOccurred(socket->errorString());
}

void CollabSession::handleMessage(const QJsonObject &obj)
{
    const QString type = obj["type"].toString();
    const QString file = obj["file"].toString();

    if (type == "kicked") {
        emit kicked();
        return;
    }

    if (type == "session_report") {
        emit sessionReportReady(parseSessionReport(obj["data"].toObject()));
        return;
    }

    if (type == "session_report_ai") {
        emit sessionAiInsightsReady(parseAiInsights(obj["data"].toObject()));
        return;
    }

    if (type == "project_init") {
        QStringList files;
        for (const QJsonValue &v : obj["files"].toArray())
            files.append(v.toString());
        fileList_ = files;
        if (obj["mode"].toString() == "readonly")
            mode_ = Mode::ReadOnly;
        emit projectInitReceived(obj["host"].toInt(), files);
        return;
    }

    if (type == "run_output") {
        emit runOutputReceived(obj["text"].toString());
        return;
    }

    if (type == "user_list") {
        QList<int> ids;
        for (const QJsonValue &v : obj["siteIds"].toArray())
            if (!v.isNull())
                ids.append(v.toInt());
        emit usersUpdated(ids);
        return;
    }

    if (type == "cursor_leave") {
        const int sid = obj["siteId"].toInt();
        if (!file.isEmpty()) {
            cursorCache[file].remove(sid);
            if (active.contains(file))
                active[file]->handleIncomingMessage(obj);
        } else {
            for (auto &fileCursors : cursorCache)
                fileCursors.remove(sid);
            for (auto *mgr : active.values())
                mgr->handleIncomingMessage(obj);
        }
        return;
    }

    if (type == "file_focus") {
        const int sid = obj["siteId"].toInt();
        if (sid != siteId_)
            emit remoteFileFocusChanged(sid, file);
        return;
    }

    if (type == "file_create") {
        const QString fp = obj["file"].toString();
        if (!fileList_.contains(fp))
            fileList_.append(fp);
        textCache[fp] = obj["text"].toString();
        emit remoteFileCreated(fp);
        return;
    }

    if (type == "file_delete") {
        const QString fp = obj["file"].toString();
        fileList_.removeAll(fp);
        textCache.remove(fp);
        fileStates.remove(fp);
        if (auto *mgr = active.take(fp)) {
            lastUsed.remove(fp);
            mgr->deleteLater();
        }
        emit remoteFileDeleted(fp);
        return;
    }

    if (type == "file_rename") {
        const QString old = obj["old"].toString();
        const QString newer = obj["new"].toString();
        const int idx = fileList_.indexOf(old);
        if (idx >= 0)
            fileList_[idx] = newer;
        if (textCache.contains(old))
            textCache[newer] = textCache.take(old);
        if (fileStates.contains(old))
            fileStates[newer] = fileStates.take(old);
        if (auto *mgr = active.take(old)) {
            mgr->setFilePath(newer);
            active[newer] = mgr;
            lastUsed[newer] = lastUsed.take(old);
        }
        emit remoteFileRenamed(old, newer);
        return;
    }

    if (type == "snapshot") {
        if (!file.isEmpty()) {
            textCache[file] = obj["text"].toString();
            fileStates.remove(file);
            if (!fileList_.contains(file))
                fileList_.append(file);
        }
        if (!file.isEmpty() && active.contains(file))
            active[file]->handleIncomingMessage(obj);
        return;
    }

    if (type == "cursor") {
        if (!file.isEmpty()) {
            const int sid = obj["siteId"].toInt();
            const int pos = obj["position"].toInt();
            if (sid != siteId_)
                cursorCache[file][sid] = pos;
            if (active.contains(file))
                active[file]->handleIncomingMessage(obj);
        }
        return;
    }

    if (type == "insert" || type == "delete" || type == "undelete") {
        if (file.isEmpty())
            return;
        if (active.contains(file))
            active[file]->handleIncomingMessage(obj);
        else
            applyOpToInactive(file, obj);
        lastUsed[file] = QDateTime::currentDateTime();
        return;
    }
}

void CollabSession::sendMessage(const QJsonObject &msg)
{
    if (!isConnected())
        return;
    socket->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
}

void CollabSession::kickUser(int siteId)
{
    QJsonObject msg;
    msg["type"] = "kick";
    msg["siteId"] = siteId;
    sendMessage(msg);
}

void CollabSession::requestSessionReport()
{
    QJsonObject msg;
    msg["type"] = "session_report_request";
    sendMessage(msg);
}

void CollabSession::endSession()
{
    QJsonObject msg;
    msg["type"] = "end_session";
    sendMessage(msg);
}

SessionReportData CollabSession::parseSessionReport(const QJsonObject &data)
{
    SessionReportData r;
    r.roomName = data["room"].toString();
    r.startTime = data["start_time"].toString();
    r.endTime = data["end_time"].toString();
    r.durationSec = data["duration_sec"].toInt();

    for (const QJsonValue &pv : data["participants"].toArray()) {
        const QJsonObject po = pv.toObject();
        ParticipantStats p;
        p.siteId = po["site_id"].toInt();
        p.isHost = po["is_host"].toBool();
        p.activeSec = po["active_sec"].toInt();
        p.totalInserts = po["total_inserts"].toInt();
        p.totalDeletes = po["total_deletes"].toInt();
        for (const QJsonValue &fv : po["files_touched"].toArray())
            p.filesTouched.append(fv.toString());
        r.participants.append(p);
    }

    for (const QJsonValue &fv : data["files"].toArray()) {
        const QJsonObject fo = fv.toObject();
        FileInfo fi;
        fi.name = fo["name"].toString();
        for (const QJsonValue &ev : fo["editors"].toArray())
            fi.editorSiteIds.append(ev.toInt());
        r.files.append(fi);
    }

    return r;
}

AiInsights CollabSession::parseAiInsights(const QJsonObject &data)
{
    AiInsights ai;
    ai.available = true;
    ai.text = data["text"].toString();
    return ai;
}

void CollabSession::sendRegister()
{
    QJsonObject msg;
    msg["type"] = "register";
    msg["siteId"] = siteId_;
    msg["role"] = (role_ == Role::Host) ? "host" : "guest";

    if (role_ == Role::Host) {
        msg["mode"] = (mode_ == Mode::ReadOnly) ? "readonly" : "readwrite";
        if (!fileList_.isEmpty()) {
            QJsonArray arr;
            for (const QString &f : fileList_)
                arr.append(f);
            msg["files"] = arr;
        }
    }
    sendMessage(msg);
}
