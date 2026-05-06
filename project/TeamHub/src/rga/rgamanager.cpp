#include "rgamanager.h"
#include <QJsonArray>
#include <QFileInfo>

RGAManager::RGAManager(int siteId, QObject* parent)
    : QObject(parent)
    , socket(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
    , siteId(siteId)
    , timestamp(0)
{
    connect(socket, &QWebSocket::connected,
            this, &RGAManager::onConnected);
    connect(socket, &QWebSocket::disconnected,
            this, &RGAManager::onDisconnected);
    connect(socket, &QWebSocket::textMessageReceived,
            this, &RGAManager::onMessageReceived);
    connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
            this, &RGAManager::onError);
}

void RGAManager::connectToServer(const QString& url)
{
    socket->open(QUrl(url));
}

void RGAManager::disconnectFromServer()
{
    socket->close();
}

bool RGAManager::isConnected()
{
    return socket->state() == QAbstractSocket::ConnectedState;
}

QString RGAManager::getText()
{
    return sequence.toText();
}

void RGAManager::localInsert(int position, QChar ch)
{
    ++timestamp;

    RGAId parentId = sequence.idAtPosition(position - 1);

    RGANode node;
    node.id.timestamp = timestamp;
    node.id.siteId    = siteId;
    node.parent       = parentId;
    node.val          = ch;
    node.tombstone    = false;

    sequence.insert(node);

    QJsonObject msg;
    msg["type"] = "insert";
    msg["node"] = nodeToJson(node);
    sendMessage(msg);

    emit textChanged(sequence.toText());
}

void RGAManager::localRemove(int position)
{
    RGAId id = sequence.idAtPosition(position);

    if (id.timestamp == 0 && id.siteId == 0) return;

    sequence.remove(id);

    QJsonObject msg;
    msg["type"] = "delete";
    msg["id"]   = idToJson(id);
    sendMessage(msg);

    emit textChanged(sequence.toText());
}

void RGAManager::remoteInsert(const RGANode& node)
{
    timestamp = qMax(timestamp, node.id.timestamp) + 1;
    sequence.insert(node);
    emit textChanged(sequence.toText());
}

void RGAManager::remoteDelete(const RGAId& id)
{
    sequence.remove(id);
    emit textChanged(sequence.toText());
}

void RGAManager::onConnected()
{
    emit connected();
}

void RGAManager::onDisconnected()
{
    emit disconnected();
}

void RGAManager::onMessageReceived(const QString& message)
{
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) return;

    QJsonObject obj  = doc.object();
    QString     type = obj["type"].toString();

    if (type == "insert") {
        RGANode node = jsonToNode(obj["node"].toObject());
        remoteInsert(node);
    } else if (type == "delete") {
        RGAId id = jsonToId(obj["id"].toObject());
        remoteDelete(id);
    }
    else if(type == "snapshot"){
        QString text = obj["text"].toString();
        QString path = obj["path"].toString();
        QString filename = QFileInfo(path).fileName();
        buildFromText(text);
        emit onInitReceived(sequence.toText(), filename);
    }
}

void RGAManager::onError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    emit errorOccurred(socket->errorString());
}

QJsonObject RGAManager::idToJson(const RGAId& id)
{
    QJsonObject obj;
    obj["timestamp"] = id.timestamp;
    obj["siteId"]    = id.siteId;
    return obj;
}

QJsonObject RGAManager::nodeToJson(const RGANode& node)
{
    QJsonObject obj;
    obj["id"]        = idToJson(node.id);
    obj["parent"]    = idToJson(node.parent);
    obj["val"]       = QString(node.val);
    obj["tombstone"] = node.tombstone;
    return obj;
}

RGAId RGAManager::jsonToId(const QJsonObject& obj)
{
    RGAId id;
    id.timestamp = obj["timestamp"].toInt();
    id.siteId    = obj["siteId"].toInt();
    return id;
}

RGANode RGAManager::jsonToNode(const QJsonObject& obj)
{
    RGANode node;
    node.id        = jsonToId(obj["id"].toObject());
    node.parent    = jsonToId(obj["parent"].toObject());
    node.val       = obj["val"].toString().isEmpty()
                   ? QChar() : obj["val"].toString().at(0);
    node.tombstone = obj["tombstone"].toBool();
    return node;
}

void RGAManager::sendMessage(const QJsonObject& msg)
{
    if (!isConnected()) return;
    socket->sendTextMessage(
        QJsonDocument(msg).toJson(QJsonDocument::Compact));
}

void RGAManager::buildFromText(const QString& text)
{
    sequence.clear();

    timestamp = 0;

    RGAId parent = RGAId{};
    for (int i = 0; i < text.size(); ++i) {
        ++timestamp;

        RGANode node;
        node.id.timestamp = timestamp;
        node.id.siteId    = siteId;
        node.parent       = parent;
        node.val          = text[i];
        node.tombstone    = false;

        sequence.insert(node);

        parent = node.id;
    }
}

void RGAManager::sendInitText(const QString text, const QString path){
    QJsonObject msg;
    msg["type"] = "snapshot";
    msg["text"] = text;
    msg["path"] = path;
    sendMessage(msg);
}

void RGAManager::debug()
{
    for (const RGANode& node : sequence.rgaseq) {
        qDebug() << "VAL:" << node.val
                 << "ID:" << node.id.timestamp << node.id.siteId
                 << "PARENT:" << node.parent.timestamp << node.parent.siteId
                 << "TOMBSTONE:" << node.tombstone;
    }
}