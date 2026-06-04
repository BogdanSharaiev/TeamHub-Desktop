#ifndef RGAMANAGER_H
#define RGAMANAGER_H
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QWebSocket>
#include "rgaseq.h"
#include <functional>

class RGAManager : public QObject
{
    Q_OBJECT
public:
    explicit RGAManager(int siteId, QObject *parent = nullptr);

    void connectToServer(const QString &url);
    void disconnectFromServer();
    bool isConnected() const;

    void setFilePath(const QString &filePath);
    void setSendFunction(std::function<void(QJsonObject)> fn);
    void handleIncomingMessage(const QJsonObject &obj);

    QString getText();
    void buildFromText(const QString &text);
    void sendInitText(const QString &text, const QString &path);
    void sendCursorPosition(int scintillaPos);

    int getSiteId() const { return siteId; }
    QString getFilePath() const { return m_filePath; }

    RGASequence getSequence() const { return sequence; }
    void setSequence(const RGASequence &seq) { sequence = seq; }
    int getTimestamp() const { return timestamp; }
    void setTimestamp(int ts) { timestamp = ts; }

    static RGANode nodeFromJson(const QJsonObject &obj);
    static RGAId idFromJson(const QJsonObject &obj);

    void debug();

public slots:
    void localInsert(int position, QChar ch);
    void localRemove(int position);

signals:
    void textChanged(const QString &newText);
    void remoteTextChanged(const QString &newText);
    void connected();
    void disconnected();
    void errorOccurred(const QString &error);
    void onInitReceived(QString text, QString filename);
    void remoteCursorMoved(int siteId, int position);
    void remoteCursorLeft(int siteId);
    void usersUpdated(QList<int> siteIds);

private slots:
    void onConnected();
    void onDisconnected();
    void onRawMessage(const QString &message);
    void onError(QAbstractSocket::SocketError error);

private:
    void processMessage(const QJsonObject &obj);
    void remoteInsert(const RGANode &node);
    void remoteDelete(const RGAId &id);
    void registerWithServer();
    QJsonObject nodeToJson(const RGANode &node) const;
    QJsonObject idToJson(const RGAId &id) const;
    void sendMessage(QJsonObject msg);
    QJsonArray sequenceToJson() const;
    void sequenceFromJson(const QJsonArray &arr);

    RGASequence sequence;
    QWebSocket *socket;
    int siteId;
    int timestamp;

    QString m_filePath;
    std::function<void(QJsonObject)> m_sendFn;
};
#endif
