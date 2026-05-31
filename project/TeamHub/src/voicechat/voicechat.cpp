#include "voicechat.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkDatagram>
#include <QHostAddress>
#include <QMediaDevices>

static constexpr const char* STUN_HOST      = "stun.l.google.com";
static constexpr quint16     STUN_PORT      = 19302;
static constexpr int         PUNCH_INTERVAL = 500;
static constexpr int         STUN_TIMEOUT   = 5000;

static constexpr quint16 STUN_BINDING_REQUEST    = 0x0001;
static constexpr quint16 STUN_BINDING_RESPONSE   = 0x0101;
static constexpr quint32 STUN_MAGIC_COOKIE       = 0x2112A442;
static constexpr quint16 ATTR_MAPPED_ADDRESS     = 0x0001;
static constexpr quint16 ATTR_XOR_MAPPED_ADDRESS = 0x0020;

static const QByteArray PUNCH_PAYLOAD = QByteArrayLiteral("PUNCH");

QAudioFormat VoiceChat::audioFormat()
{
    QAudioFormat fmt;
    fmt.setSampleRate(16000);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Int16);
    return fmt;
}

VoiceChat::VoiceChat(QObject* parent)
    : QObject(parent)
    , webSocket(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
    , udpSocket(new QUdpSocket(this))
    , punchTimer(new QTimer(this))
    , stunTimer(new QTimer(this))
{
    connect(webSocket, &QWebSocket::connected,
            this, &VoiceChat::onWebSocketConnected);
    connect(webSocket, &QWebSocket::disconnected,
            this, &VoiceChat::onWebSocketDisconnected);
    connect(webSocket, &QWebSocket::textMessageReceived,
            this, &VoiceChat::onWebSocketTextMessageReceived);

    connect(udpSocket, &QUdpSocket::readyRead,
            this, &VoiceChat::onUdpReadyRead);

    punchTimer->setInterval(PUNCH_INTERVAL);
    connect(punchTimer, &QTimer::timeout,
            this, &VoiceChat::onPunchTimerTimeout);

    stunTimer->setSingleShot(true);
    stunTimer->setInterval(STUN_TIMEOUT);
    connect(stunTimer, &QTimer::timeout,
            this, &VoiceChat::onStunTimeout);

    publicId = QRandomGenerator::global()->bounded(100000, 999999);

    flushTimer = new QTimer(this);
    flushTimer->setSingleShot(true);
    flushTimer->setInterval(40);
    connect(flushTimer, &QTimer::timeout, this, [this]() {
        if (captureBuffer.isEmpty() || !opusEncoder) return;

        int needed = OPUS_FRAME_SIZE * 2 - captureBuffer.size() % (OPUS_FRAME_SIZE * 2);
        if (needed != OPUS_FRAME_SIZE * 2)
            captureBuffer.append(QByteArray(needed, '\0'));

        while (captureBuffer.size() >= OPUS_FRAME_SIZE * 2) {
            const opus_int16* pcmData =
                reinterpret_cast<const opus_int16*>(captureBuffer.constData());
            QByteArray encoded(4000, '\0');
            int len = opus_encode(opusEncoder, pcmData, OPUS_FRAME_SIZE,
                                  reinterpret_cast<uchar*>(encoded.data()), 4000);
            if (len > 0) {
                encoded.resize(len);
                for (const PeerInfo& peer : std::as_const(peers))
                    if (peer.connected)
                        udpSocket->writeDatagram(encoded, QHostAddress(peer.ip), peer.port);
            }
            captureBuffer.remove(0, OPUS_FRAME_SIZE * 2);
        }
    });
}

VoiceChat::~VoiceChat()
{
    stopCall();
    punchTimer->stop();
    stunTimer->stop();
    webSocket->close();
    udpSocket->close();
}

void VoiceChat::connectToServer(const QString& host, quint16 port)
{
    serverHost = host;
    serverPort = port;

    if (udpSocket->state() != QAbstractSocket::BoundState) {
        if (!udpSocket->bind(QHostAddress::AnyIPv4, 0,
                               QAbstractSocket::ShareAddress |
                                   QAbstractSocket::ReuseAddressHint)) {
            emit statusChanged("UDP bind failed: " + udpSocket->errorString());
            return;
        }
    }

    emit statusChanged("Performing STUN discovery…");
    performStun();
}

void VoiceChat::disconnectFromServer()
{
    stopCall();
    webSocket->close();
    for (PeerInfo& peer : peers)
        destroyPeerSink(peer);
    peers.clear();
}

void VoiceChat::startCall()
{
    QAudioFormat fmt = audioFormat();
    QAudioDevice inputDev = QMediaDevices::defaultAudioInput();

    if (!inputDev.isFormatSupported(fmt)) {
        emit statusChanged("Microphone does not support 16kHz/16-bit/mono.");
        return;
    }

    audioSource = new QAudioSource(inputDev, fmt, this);
    audioSource->setBufferSize(4096);
    audioInput = audioSource->start();
    int err;
    opusEncoder = opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK) {
        emit statusChanged("Opus encoder error: " + QString(opus_strerror(err)));
        return;
    }
    opus_encoder_ctl(opusEncoder, OPUS_SET_BITRATE(24000)); // 24 kbps

    opusDecoder = opus_decoder_create(16000, 1, &err);
    if (err != OPUS_OK) {
        emit statusChanged("Opus decoder error: " + QString(opus_strerror(err)));
        return;
    }
    connect(audioInput, &QIODevice::readyRead,
            this, &VoiceChat::onAudioInputReady);

    for (PeerInfo& peer : peers)
        if (peer.connected)
            createPeerSink(peer);

    emit statusChanged(QString("Call active — %1 peer(s)").arg(peers.size()));
}

void VoiceChat::stopCall()
{
    punchTimer->stop();
    if (audioSource) {
        audioSource->stop();
        delete audioSource;
        audioSource = nullptr;
        audioInput  = nullptr;
    }
    for (PeerInfo& peer : peers)
        destroyPeerSink(peer);
    if (opusEncoder) {
        opus_encoder_destroy(opusEncoder);
        opusEncoder = nullptr;
    }
    if (opusDecoder) {
        opus_decoder_destroy(opusDecoder);
        opusDecoder = nullptr;
    }
}

bool VoiceChat::isCallActive() const
{
    return audioSource != nullptr;
}

bool VoiceChat::isConnected() const
{
    return webSocket->state() == QAbstractSocket::ConnectedState;
}

void VoiceChat::performStun()
{
    QByteArray pkt(20, '\0');
    auto* d = reinterpret_cast<uchar*>(pkt.data());
    qToBigEndian<quint16>(STUN_BINDING_REQUEST, d);
    qToBigEndian<quint16>(0,                    d + 2);
    qToBigEndian<quint32>(STUN_MAGIC_COOKIE,    d + 4);

    stunTransactionId.resize(12);
    for (int i = 0; i < 12; ++i)
        stunTransactionId[i] = static_cast<char>(
            QRandomGenerator::global()->bounded(256));
    std::memcpy(d + 8, stunTransactionId.constData(), 12);

    QHostInfo::lookupHost(STUN_HOST, this, [this, pkt](const QHostInfo& info) {
        if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
            emit statusChanged("STUN DNS lookup failed: " + info.errorString());
            return;
        }
        udpSocket->writeDatagram(pkt, info.addresses().constFirst(), STUN_PORT);
        stunTimer->start();
    });
}

void VoiceChat::parseStunResponse(const QByteArray& data)
{
    stunTimer->stop();
    if (data.size() < 20) return;
    const auto* d = reinterpret_cast<const uchar*>(data.constData());

    if (qFromBigEndian<quint16>(d)     != STUN_BINDING_RESPONSE) return;
    if (qFromBigEndian<quint32>(d + 4) != STUN_MAGIC_COOKIE)     return;

    int offset = 20;
    while (offset + 4 <= data.size()) {
        quint16 attrType = qFromBigEndian<quint16>(d + offset);
        quint16 attrLen  = qFromBigEndian<quint16>(d + offset + 2);
        offset += 4;
        if (offset + attrLen > data.size()) break;

        auto extract = [&](bool xor_) -> bool {
            if (attrLen < 8 || static_cast<quint8>(data[offset + 1]) != 0x01)
                return false;
            quint16 rawPort = qFromBigEndian<quint16>(d + offset + 2);
            quint32 rawIp   = qFromBigEndian<quint32>(d + offset + 4);
            if (xor_) {
                rawPort ^= quint16(STUN_MAGIC_COOKIE >> 16);
                rawIp   ^= STUN_MAGIC_COOKIE;
            }
            publicPort = rawPort;
            publicIp   = QString("%1.%2.%3.%4")
                             .arg((rawIp>>24)&0xFF).arg((rawIp>>16)&0xFF)
                             .arg((rawIp>>8)&0xFF).arg(rawIp&0xFF);
            return true;
        };

        if (attrType == ATTR_XOR_MAPPED_ADDRESS && extract(true))  break;
        if (attrType == ATTR_MAPPED_ADDRESS      && extract(false)) break;
        offset += (attrLen + 3) & ~3;
    }

    if (publicIp.isEmpty() || publicPort == 0) {
        emit statusChanged("STUN: could not parse mapped address.");
        return;
    }

    emit statusChanged(QString("Public: %1:%2 — connecting…")
                           .arg(publicIp).arg(publicPort));

    webSocket->open(QUrl(QString("ws://%1:%2")
                               .arg(serverHost).arg(serverPort)));
}

void VoiceChat::onStunTimeout()
{
    emit statusChanged("STUN timed out.");
}

void VoiceChat::onWebSocketConnected()
{
    emit statusChanged(QString("Connected. Public: %1:%2")
                           .arg(publicIp).arg(publicPort));
    registerWithServer();
    emit connectedToServer();
}

void VoiceChat::onWebSocketDisconnected()
{
    stopCall();
    punchTimer->stop();
    for (PeerInfo& peer : peers)
        destroyPeerSink(peer);
    peers.clear();
    emit disconnectedFromServer();
    emit statusChanged("Disconnected from server.");
}

void VoiceChat::onWebSocketTextMessageReceived(const QString& message)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) return;
    QJsonObject obj = doc.object();
    if (obj.value("type").toString() == "peer_list")
        updatePeerList(obj.value("peers").toArray());
}

void VoiceChat::registerWithServer()
{
    QJsonObject msg;
    msg["type"] = "register";
    msg["ip"]   = publicIp;
    msg["port"] = static_cast<int>(publicPort);
    msg["id"]   = publicId;

    webSocket->sendTextMessage(
        QJsonDocument(msg).toJson(QJsonDocument::Compact));
}

void VoiceChat::updatePeerList(const QJsonArray& peerArray)
{
    struct Entry
    {
        QString ip;
        quint16 port;
        int id;
    };

    QList<Entry> incoming;

    for (const QJsonValue& v : peerArray)
    {
        QJsonObject o = v.toObject();

        Entry e{
            o.value("ip").toString(),
            quint16(o.value("port").toInt()),
            o.value("id").toInt()
        };

        if (e.id == publicId)
            continue;

        incoming.append(e);
    }

    for (int i = peers.size() - 1; i >= 0; --i)
    {
        bool found = false;

        for (const Entry& e : incoming)
        {
            if (e.id == peers[i].id)
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            emit peerDisconnected(peers[i].ip, peers[i].port);

            destroyPeerSink(peers[i]);

            peers.removeAt(i);
        }
    }

    for (const Entry& e : incoming)
    {
        bool already = false;

        for (const PeerInfo& p : std::as_const(peers))
        {
            if (p.id == e.id)
            {
                already = true;
                break;
            }
        }

        if (!already)
        {
            PeerInfo peer;
            peer.ip   = e.ip;
            peer.port = e.port;
            peer.id   = e.id;

            peers.append(peer);

            emit statusChanged(
                QString("New peer: %1:%2 [%3]")
                    .arg(peer.ip)
                    .arg(peer.port)
                    .arg(peer.id));
        }
    }

    bool anyUnconnected = false;

    for (const PeerInfo& p : std::as_const(peers))
    {
        if (!p.connected)
        {
            anyUnconnected = true;
            break;
        }
    }

    if (anyUnconnected && !punchTimer->isActive())
        punchTimer->start();
}

void VoiceChat::onPunchTimerTimeout()
{
    bool allDone = true;
    for (const PeerInfo& peer : std::as_const(peers)) {
        if (!peer.connected) {
            allDone = false;
            udpSocket->writeDatagram(
                PUNCH_PAYLOAD, QHostAddress(peer.ip), peer.port);
        }
    }
    if (allDone) {
        punchTimer->stop();
        emit statusChanged(QString("All %1 peer(s) connected.")
                               .arg(peers.size()));
    }
}

void VoiceChat::onUdpReadyRead()
{
    while (udpSocket->hasPendingDatagrams()) {
        QNetworkDatagram dg         = udpSocket->receiveDatagram();
        QByteArray       data       = dg.data();
        QHostAddress     senderAddr = dg.senderAddress();
        quint16          senderPort = static_cast<quint16>(dg.senderPort());

        if (data.size() >= 20) {
            const auto* raw = reinterpret_cast<const uchar*>(data.constData());
            if (qFromBigEndian<quint32>(raw + 4) == STUN_MAGIC_COOKIE) {
                parseStunResponse(data);
                continue;
            }
        }

        QString senderIp = senderAddr.toString().remove("::ffff:");
        int idx = -1;
        for (int i = 0; i < peers.size(); ++i) {
            const PeerInfo& p = peers[i];
            if (p.port == senderPort &&
                p.ip == senderIp)
            {
                idx = i;
                break;
            }
        }

        if (data == PUNCH_PAYLOAD) {
            if (idx != -1 && !peers[idx].connected)
                markPeerConnected(idx);
            continue;
        }

        if (idx != -1) {
            PeerInfo& peer = peers[idx];
            qDebug() << "UDP received"
                     << data.size()
                     << senderIp
                     << senderPort;
            if (!peer.connected) markPeerConnected(idx);
            if (peer.output && opusDecoder) {
                QByteArray pcmOut(OPUS_FRAME_SIZE * sizeof(opus_int16), '\0');
                int samples = opus_decode(
                    opusDecoder,
                    reinterpret_cast<const unsigned char*>(data.constData()),
                    data.size(),
                    reinterpret_cast<opus_int16*>(pcmOut.data()),
                    OPUS_FRAME_SIZE,
                    0);

                if (samples < 0)
                {
                    qDebug() << "opus_decode error:"
                             << opus_strerror(samples);
                    continue;
                }

                qDebug() << "decoded =" << samples;

                qDebug() << "decoded samples =" << samples;
                if (samples > 0)
                    peer.output->write(pcmOut.left(samples * 2));
            }
        }
    }
}

void VoiceChat::onAudioInputReady()
{
    if (!audioInput || !opusEncoder)
        return;

    captureBuffer.append(audioInput->readAll());
    flushTimer->start();

    while (captureBuffer.size() >= OPUS_FRAME_SIZE * 2)
    {
        const opus_int16* pcmData =
            reinterpret_cast<const opus_int16*>(captureBuffer.constData());

        QByteArray encoded(4000, '\0');

        int encodedLen = opus_encode(
            opusEncoder,
            pcmData,
            OPUS_FRAME_SIZE,
            reinterpret_cast<unsigned char*>(encoded.data()),
            encoded.size());

        qDebug() << "encoded =" << encodedLen;

        if (encodedLen > 0)
        {
            encoded.resize(encodedLen);

            for (const PeerInfo& peer : std::as_const(peers))
            {
                if (peer.connected)
                {
                    udpSocket->writeDatagram(
                        encoded,
                        QHostAddress(peer.ip),
                        peer.port);
                }
            }
        }
        else
        {
            qDebug() << "opus_encode error:"
                     << opus_strerror(encodedLen);
        }

        captureBuffer.remove(0, OPUS_FRAME_SIZE * 2);
    }
}

void VoiceChat::createPeerSink(PeerInfo& peer)
{
    if (peer.sink) return;
    peer.sink   = new QAudioSink(QMediaDevices::defaultAudioOutput(),
                               audioFormat(), this);
    peer.output = peer.sink->start();
}

void VoiceChat::destroyPeerSink(PeerInfo& peer)
{
    if (!peer.sink) return;
    peer.sink->stop();
    delete peer.sink;
    peer.sink   = nullptr;
    peer.output = nullptr;
}

void VoiceChat::markPeerConnected(int index)
{
    PeerInfo& peer = peers[index];
    peer.connected = true;
    emit peerConnected(peer.ip, peer.port);
    emit statusChanged(QString("P2P link up: %1:%2")
                           .arg(peer.ip).arg(peer.port));
    if (audioSource) createPeerSink(peer);
}