#ifndef RGAMANAGER_H
#define RGAMANAGER_H
#include <QObject>
#include <QWebSocket>
#include <QJsonObject>
#include <QJsonDocument>
#include "rgaseq.h"

class RGAManager : public QObject
{
    Q_OBJECT
public:
    explicit RGAManager(int siteId, QObject* parent = nullptr);
    void localInsert(int position, QChar ch);
    void localRemove(int position);
    void connectToServer(const QString& url);
    void disconnectFromServer();
    bool isConnected();
    QString getText();
    void buildFromText(const QString& text);
    void sendInitText(const QString& text, const QString& path);
    void debug();
signals:
    void textChanged(const QString& newText);
    void remoteTextChanged(const QString& newText);
    void connected();
    void disconnected();
    void errorOccurred(const QString& error);
    void onInitReceived(QString text, QString filename);

private slots:
    void onConnected();
    void onDisconnected();
    void onMessageReceived(const QString& message);
    void onError(QAbstractSocket::SocketError error);

private:
    void remoteInsert(const RGANode& node);
    void remoteDelete(const RGAId& id);
    QJsonObject nodeToJson(const RGANode& node);
    QJsonObject idToJson(const RGAId& id) const;
    RGANode     jsonToNode(const QJsonObject& obj);
    RGAId       jsonToId(const QJsonObject& obj) const;
    void sendMessage(const QJsonObject& msg);
    QJsonArray sequenceToJson() const;
    void sequenceFromJson(const QJsonArray& arr);

    RGASequence sequence;
    QWebSocket* socket;
    int         siteId;
    int         timestamp;
};

#endif // RGAMANAGER_H