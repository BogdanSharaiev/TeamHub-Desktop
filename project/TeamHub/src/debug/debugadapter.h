#ifndef DEBUGADAPTER_H
#define DEBUGADAPTER_H

#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

class DebugAdapter : public QObject
{
    Q_OBJECT
public:
    struct Var
    {
        QString name;
        QString value;
        int ref = 0;
    };

    struct FrameInfo
    {
        QString label;
        int frameId = -1;
    };

    explicit DebugAdapter(QObject *parent = nullptr);
    ~DebugAdapter() override;

    void startDebugging(const QString &scriptPath, const QSet<int> &breakpoints);
    void updateBreakpoints(const QSet<int> &lines);
    void stop();
    void continueExec();
    void stepOver();
    void stepIn();
    void stepOut();

    bool isRunning() const { return running; }
    void requestSubVariables(int parentRef);
    void requestFrameVariables(int frameId);

signals:
    void stopped(const QString &filePath, int line, const QString &reason);
    void continued();
    void terminated();
    void variablesReady(const QList<DebugAdapter::Var> &vars);
    void subVariablesReady(int parentRef, const QList<DebugAdapter::Var> &vars);
    void callStackReady(const QList<DebugAdapter::FrameInfo> &frames);
    void logMessage(const QString &text);

private slots:
    void onConnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError err);
    void onProcessStarted();
    void tryConnect();

private:
    QProcess *process = nullptr;
    QTcpSocket *socket = nullptr;
    QTimer *connectTimer = nullptr;
    QByteArray buffer;
    QString scriptPath;
    QSet<int> breakpoints;
    int seq = 1;
    int threadId = -1;
    int frameId = -1;
    bool running = false;
    bool configured = false;
    QMap<int, int> varParentRef;
    int connectAttempts = 0;
    quint16 port = 0;
    QMap<int, QString> pendingRequests;

    void sendRequest(const QString &command, const QJsonObject &args = {});
    void processMessage(const QByteArray &data);
    void handleEvent(const QJsonObject &msg);
    void handleResponse(const QJsonObject &msg);
    void sendInitialize();
    void sendAttach();
    void sendSetBreakpoints();
    void sendConfigurationDone();
    void requestStackTrace();
    void requestScopes(int frameId);
    void requestVariables(int variablesReference);
};

#endif // DEBUGADAPTER_H
