#ifndef VOICECHAT_H
#define VOICECHAT_H

#include <QAudioSink>
#include <QAudioSource>
#include <QHostInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QNetworkInterface>
#include <QObject>
#include <QRandomGenerator>
#include <QTimer>
#include <QUdpSocket>
#include <QWebSocket>
#include <QtEndian>
#include <cstring>

#include <opus/opus.h>

struct PeerInfo
{
    int id = 0;
    QString ip;
    quint16 port = 0;
    bool connected = false;
    QAudioSink *sink = nullptr;
    QIODevice *output = nullptr;
    OpusDecoder *decoder = nullptr;
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
    void setRoom(const QString &r);

    bool micMuted = false;
    bool audioMuted = false;

    void setMicMuted(bool m) { micMuted = m; }
    void setAudioMuted(bool m) { audioMuted = m; }

signals:
    void statusChanged(const QString &status);
    void peerConnected(const QString &ip, quint16 port);
    void peerDisconnected(const QString &ip, quint16 port);
    void connectedToServer();
    void disconnectedFromServer();
    void peersUpdated(const QStringList &ids);
    void roomsUpdated(const QStringList &rooms);

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

    void setMode(const QString &m);

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

    OpusEncoder *opusEncoder = nullptr;
    QByteArray captureBuffer;
    static constexpr int OPUS_FRAME_SIZE = 320;
    QTimer *flushTimer;

    QString room = "default";
    QString mode = "hybrid"; // relay / hybrid / p2p
};

#endif // VOICECHAT_H