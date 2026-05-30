#ifndef VOICECHAT_H
#define VOICECHAT_H

#include <QAudioSink>
#include <QAudioSource>
#include <QHostInfo>
#include <QIODevice>
#include <QObject>
#include <QRandomGenerator>
#include <QTimer>
#include <QUdpSocket>
#include <QWebSocket>
#include <QtEndian>
#include <cstring>
#include <QJsonArray>

#include <opus/opus.h>

struct PeerInfo
{
    int id = 0;
    QString ip;
    quint16 port = 0;
    bool connected = false;
    QAudioSink *sink = nullptr;
    QIODevice *output = nullptr;
};

class VoiceChat : public QObject
{
    Q_OBJECT

public:
    explicit VoiceChat(QObject *parent = nullptr);
    ~VoiceChat();

    void connectToServer(const QString &host, quint16 port);
    void disconnectFromServer();
    void startCall();
    void stopCall();
    bool isCallActive() const;
    bool isConnected() const;

signals:
    void statusChanged(const QString &status);
    void peerConnected(const QString &ip, quint16 port);
    void peerDisconnected(const QString &ip, quint16 port);
    void connectedToServer();
    void disconnectedFromServer();

private slots:
    void onUdpReadyRead();
    void onPunchTimerTimeout();
    void onStunTimeout();
    void onWebSocketConnected();
    void onWebSocketDisconnected();
    void onWebSocketTextMessageReceived(const QString &message);
    void onAudioInputReady();

private:
    void performStun();
    void parseStunResponse(const QByteArray &data);
    void registerWithServer();
    void updatePeerList(const QJsonArray &peerArray);
    void markPeerConnected(int index);
    void createPeerSink(PeerInfo &peer);
    void destroyPeerSink(PeerInfo &peer);

    static QAudioFormat audioFormat();

    QWebSocket *webSocket;
    QUdpSocket *udpSocket;
    QTimer *punchTimer;
    QTimer *stunTimer;

    QAudioSource *audioSource = nullptr;
    QIODevice *audioInput = nullptr;

    QList<PeerInfo> peers;

    QString publicIp;
    int publicId;
    quint16 publicPort = 0;
    QByteArray stunTransactionId;

    QString serverHost;
    quint16 serverPort = 0;

    OpusEncoder* opusEncoder = nullptr;
    OpusDecoder* opusDecoder = nullptr;
    QByteArray captureBuffer;
    static constexpr int OPUS_FRAME_SIZE = 320;
};

#endif // VOICECHAT_H