#include "debugadapter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

static const int kMaxConnectAttempts = 25;

DebugAdapter::DebugAdapter(QObject *parent)
    : QObject(parent)
{
    process = new QProcess(this);
    socket = new QTcpSocket(this);
    connectTimer = new QTimer(this);
    connectTimer->setInterval(300);
    connectTimer->setSingleShot(false);

    connect(socket, &QTcpSocket::connected, this, &DebugAdapter::onConnected);
    connect(socket, &QTcpSocket::readyRead, this, &DebugAdapter::onReadyRead);
    connect(socket, &QAbstractSocket::errorOccurred, this, &DebugAdapter::onSocketError);
    connect(connectTimer, &QTimer::timeout, this, &DebugAdapter::tryConnect);
    connect(process, &QProcess::started, this, &DebugAdapter::onProcessStarted);
    connect(process, &QProcess::readyReadStandardOutput, this, [this]() {
        emit logMessage(QString::fromUtf8(process->readAllStandardOutput()));
    });
    connect(process, &QProcess::readyReadStandardError, this, [this]() {
        emit logMessage(QString::fromUtf8(process->readAllStandardError()));
    });
    connect(process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this](int, QProcess::ExitStatus) {
                if (running) {
                    running = false;
                    emit terminated();
                }
            });
}

DebugAdapter::~DebugAdapter()
{
    stop();
}

void DebugAdapter::startDebugging(const QString &scriptPath, const QSet<int> &breakpoints)
{
    stop();

    {
        QTcpServer portFinder;
        portFinder.listen(QHostAddress::LocalHost, 0);
        port = portFinder.serverPort();
    }

    this->scriptPath = scriptPath;
    this->breakpoints = breakpoints;
    seq = 1;
    threadId = -1;
    frameId = -1;
    running = true;
    configured = false;
    connectAttempts = 0;
    buffer.clear();
    pendingRequests.clear();

    process->start("python",
                   {"-m",
                    "debugpy",
                    "--listen",
                    QString("127.0.0.1:%1").arg(port),
                    "--wait-for-client",
                    scriptPath});
}

void DebugAdapter::onProcessStarted()
{
    emit logMessage("debugpy started — connecting...\n");
    connectTimer->start();
}

void DebugAdapter::tryConnect()
{
    if (socket->state() != QAbstractSocket::UnconnectedState)
        return;
    if (++connectAttempts > kMaxConnectAttempts) {
        connectTimer->stop();
        emit logMessage("Could not connect to debugpy\n");
        running = false;
        emit terminated();
        return;
    }
    socket->connectToHost("127.0.0.1", port);
}

void DebugAdapter::onConnected()
{
    connectTimer->stop();
    emit logMessage("Connected to debugpy\n");
    sendInitialize();
}

void DebugAdapter::onSocketError(QAbstractSocket::SocketError err)
{
    if (err == QAbstractSocket::ConnectionRefusedError && connectAttempts <= kMaxConnectAttempts
        && connectTimer->isActive()) {
        socket->abort();
        return;
    }
    emit logMessage(QString("Socket error: %1\n").arg(socket->errorString()));
}

void DebugAdapter::stop()
{
    connectTimer->stop();
    if (socket->state() != QAbstractSocket::UnconnectedState) {
        socket->abort();
    }
    if (process->state() != QProcess::NotRunning) {
        process->terminate();
        if (!process->waitForFinished(2000))
            process->kill();
    }
    if (running) {
        running = false;
        emit terminated();
    }
    buffer.clear();
}

void DebugAdapter::continueExec()
{
    QJsonObject args;
    args["threadId"] = threadId;
    sendRequest("continue", args);
}

void DebugAdapter::stepOver()
{
    QJsonObject args;
    args["threadId"] = threadId;
    sendRequest("next", args);
}

void DebugAdapter::stepIn()
{
    QJsonObject args;
    args["threadId"] = threadId;
    sendRequest("stepIn", args);
}

void DebugAdapter::stepOut()
{
    QJsonObject args;
    args["threadId"] = threadId;
    sendRequest("stepOut", args);
}

void DebugAdapter::sendRequest(const QString &command, const QJsonObject &args)
{
    QJsonObject msg;
    msg["seq"] = seq;
    msg["type"] = "request";
    msg["command"] = command;
    if (!args.isEmpty())
        msg["arguments"] = args;

    pendingRequests[seq] = command;
    ++seq;

    QByteArray body = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    QByteArray header = QStringLiteral("Content-Length: %1\r\n\r\n").arg(body.size()).toUtf8();
    socket->write(header + body);
}

void DebugAdapter::onReadyRead()
{
    buffer += socket->readAll();

    while (true) {
        int headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            break;

        int contentLength = -1;
        const QByteArray header = buffer.left(headerEnd);
        for (const QByteArray &line : header.split('\n')) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.startsWith("Content-Length:")) {
                contentLength = trimmed.mid(15).trimmed().toInt();
                break;
            }
        }
        if (contentLength < 0)
            break;

        const int bodyStart = headerEnd + 4;
        if (buffer.size() < bodyStart + contentLength)
            break;

        const QByteArray body = buffer.mid(bodyStart, contentLength);
        buffer.remove(0, bodyStart + contentLength);
        processMessage(body);
    }
}

void DebugAdapter::processMessage(const QByteArray &data)
{
    QJsonParseError parseError;
    const QJsonObject msg = QJsonDocument::fromJson(data, &parseError).object();
    if (parseError.error != QJsonParseError::NoError) {
        emit logMessage(QString("[DAP] parse error: %1\n").arg(parseError.errorString()));
        return;
    }

    const QString type = msg["type"].toString();
    const QString tag = (type == "event") ? msg["event"].toString() : msg["command"].toString();
    emit logMessage(QString("[DAP] %1 %2\n").arg(type, tag));

    if (type == "event")
        handleEvent(msg);
    else if (type == "response")
        handleResponse(msg);
}

void DebugAdapter::handleEvent(const QJsonObject &msg)
{
    const QString event = msg["event"].toString();
    const QJsonObject body = msg["body"].toObject();

    if (event == "initialized") {
        emit logMessage("[DAP] initialized — configuring\n");
        sendSetBreakpoints();
        sendConfigurationDone();
    } else if (event == "stopped") {
        threadId = body["threadId"].toInt();
        const QString reason = body["reason"].toString();
        emit logMessage(QString("Stopped: %1\n").arg(reason));
        requestStackTrace();
    } else if (event == "continued") {
        emit continued();
    } else if (event == "terminated" || event == "exited") {
        running = false;
        emit terminated();
    } else if (event == "output") {
        const QString text = body["output"].toString();
        if (!text.isEmpty())
            emit logMessage(text);
    }
}

void DebugAdapter::handleResponse(const QJsonObject &msg)
{
    const int reqSeq = msg["request_seq"].toInt();
    const QString command = pendingRequests.take(reqSeq);

    if (!msg["success"].toBool()) {
        emit logMessage(QString("DAP error (%1): %2\n").arg(command, msg["message"].toString()));
        return;
    }

    const QJsonObject body = msg["body"].toObject();

    if (command == "initialize") {
        sendAttach();
    } else if (command == "attach") {
        emit logMessage("[DAP] attached\n");
    } else if (command == "stackTrace") {
        const QJsonArray frames = body["stackFrames"].toArray();

        auto isUserFrame = [](const QJsonObject &f) {
            const QString p
                = QString(f["source"].toObject()["path"].toString()).replace('\\', '/').toLower();
            if (p.isEmpty())
                return false;
            if (p.contains("/lib/python"))
                return false;
            if (p.contains("/site-packages/debugpy"))
                return false;
            if (p.contains("/site-packages/ptvsd"))
                return false;
            return true;
        };

        QList<FrameInfo> frameList;
        int userTopIdx = -1;
        for (int i = 0; i < frames.size(); ++i) {
            const QJsonObject frame = frames[i].toObject();
            if (!isUserFrame(frame))
                continue;
            if (userTopIdx < 0)
                userTopIdx = i;
            frameList << FrameInfo{QString("%1  —  %2:%3")
                                       .arg(frame["name"].toString(),
                                            frame["source"].toObject()["path"].toString())
                                       .arg(frame["line"].toInt()),
                                   frame["id"].toInt()};
        }
        emit callStackReady(frameList);

        const int topIdx = (userTopIdx >= 0) ? userTopIdx : 0;
        if (!frames.isEmpty()) {
            const QJsonObject top = frames[topIdx].toObject();
            frameId = top["id"].toInt();
            const int line = top["line"].toInt();
            const QString path = top["source"].toObject()["path"].toString();
            emit logMessage(QString("[DAP] stopped at %1:%2\n").arg(path).arg(line));
            emit stopped(path, line - 1, "");
            requestScopes(frameId);
        }
    } else if (command == "scopes") {
        const QJsonArray scopes = body["scopes"].toArray();
        int ref = -1;
        for (const QJsonValue &sv : scopes) {
            const QJsonObject scope = sv.toObject();
            if (scope["name"].toString() == "Locals") {
                ref = scope["variablesReference"].toInt();
                break;
            }
        }
        if (ref < 0 && !scopes.isEmpty())
            ref = scopes[0].toObject()["variablesReference"].toInt();
        if (ref > 0)
            requestVariables(ref);
    } else if (command == "variables") {
        const QJsonArray vars = body["variables"].toArray();
        QList<Var> varList;
        for (const QJsonValue &vv : vars) {
            const QJsonObject v = vv.toObject();
            varList << Var{v["name"].toString(),
                           v["value"].toString(),
                           v["variablesReference"].toInt()};
        }
        const int parentRef = varParentRef.take(reqSeq);
        if (parentRef < 0)
            emit variablesReady(varList);
        else
            emit subVariablesReady(parentRef, varList);
    }
}

void DebugAdapter::sendInitialize()
{
    QJsonObject args;
    args["adapterID"] = "debugpy";
    args["clientID"] = "teamhub";
    args["clientName"] = "TeamHub";
    args["linesStartAt1"] = true;
    args["columnsStartAt1"] = true;
    args["pathFormat"] = "path";
    args["supportsVariableType"] = true;
    args["supportsRunInTerminalRequest"] = false;
    sendRequest("initialize", args);
}

void DebugAdapter::sendSetBreakpoints()
{
    QJsonArray bps;
    QJsonArray lines;
    for (int line : breakpoints) {
        bps.append(QJsonObject{{"line", line + 1}});
        lines.append(line + 1);
    }
    QJsonObject args;
    args["source"] = QJsonObject{{"path", scriptPath}};
    args["breakpoints"] = bps;
    args["lines"] = lines;
    sendRequest("setBreakpoints", args);
}

void DebugAdapter::sendConfigurationDone()
{
    sendRequest("configurationDone");
}

void DebugAdapter::sendAttach()
{
    if (configured)
        return;
    configured = true;
    emit logMessage("[DAP] sending attach\n");
    QJsonObject connect;
    connect["host"] = "127.0.0.1";
    connect["port"] = (int) port;
    QJsonObject args;
    args["connect"] = connect;
    args["justMyCode"] = true;
    sendRequest("attach", args);
}

void DebugAdapter::requestStackTrace()
{
    QJsonObject args;
    args["threadId"] = threadId;
    args["startFrame"] = 0;
    args["levels"] = 20;
    sendRequest("stackTrace", args);
}

void DebugAdapter::requestScopes(int frameId)
{
    QJsonObject args;
    args["frameId"] = frameId;
    sendRequest("scopes", args);
}

void DebugAdapter::requestVariables(int variablesReference)
{
    const int requestSeq = seq;
    sendRequest("variables", QJsonObject{{"variablesReference", variablesReference}});
    varParentRef[requestSeq] = -1;
}

void DebugAdapter::requestSubVariables(int parentRef)
{
    const int requestSeq = seq;
    sendRequest("variables", QJsonObject{{"variablesReference", parentRef}});
    varParentRef[requestSeq] = parentRef;
}

void DebugAdapter::updateBreakpoints(const QSet<int> &lines)
{
    breakpoints = lines;
    if (running && configured)
        sendSetBreakpoints();
}

void DebugAdapter::requestFrameVariables(int frameId)
{
    this->frameId = frameId;
    requestScopes(frameId);
}
