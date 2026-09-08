#include "mqttclient.h"

#include <QAbstractSocket>
#include <QTcpSocket>
#include <QTimer>

MqttClient::MqttClient(QObject *parent)
    : QObject(parent),
      m_socket(new QTcpSocket(this)),
      m_maintenanceTimer(new QTimer(this)),
      m_reconnectTimer(new QTimer(this)),
      m_port(1883),
      m_packetId(0),
      m_reconnectSeconds(1),
      m_online(false),
      m_stopping(false),
      m_state(Stopped)
{
    m_maintenanceTimer->setInterval(5000);
    m_reconnectTimer->setSingleShot(true);
    connect(m_socket, SIGNAL(connected()), this, SLOT(socketConnected()));
    connect(m_socket, SIGNAL(disconnected()), this, SLOT(socketDisconnected()));
    connect(m_socket, SIGNAL(readyRead()), this, SLOT(socketReadyRead()));
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(m_socket, &QTcpSocket::errorOccurred, this, &MqttClient::socketError);
#else
    connect(m_socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(socketError()));
#endif
    connect(m_maintenanceTimer, SIGNAL(timeout()), this, SLOT(maintenanceTick()));
    connect(m_reconnectTimer, SIGNAL(timeout()), this, SLOT(connectToBroker()));
}

MqttClient::~MqttClient()
{
    stop();
}

void MqttClient::configure(const QString &host, quint16 port, const QString &clientId)
{
    m_host = host;
    m_port = port;
    m_clientId = clientId;
}

QString MqttClient::telemetryTopic() const
{
    return QStringLiteral("devices/%1/telemetry").arg(m_clientId);
}

QString MqttClient::commandTopic() const
{
    return QStringLiteral("devices/%1/commands").arg(m_clientId);
}

void MqttClient::start()
{
    if (m_host.isEmpty() || m_clientId.isEmpty()) {
        emit logMessage(QStringLiteral("MQTT 配置不完整"));
        return;
    }
    m_stopping = false;
    m_reconnectSeconds = 1;
    m_maintenanceTimer->start();
    connectToBroker();
}

void MqttClient::stop()
{
    m_stopping = true;
    m_reconnectTimer->stop();
    m_maintenanceTimer->stop();
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        sendPacket(0xE0, QByteArray());
        m_socket->flush();
        m_socket->disconnectFromHost();
    } else {
        m_socket->abort();
    }
    m_state = Stopped;
    setOnline(false);
}

void MqttClient::connectToBroker()
{
    if (m_stopping || m_socket->state() != QAbstractSocket::UnconnectedState)
        return;
    m_input.clear();
    m_state = Connecting;
    emit logMessage(QStringLiteral("连接 MQTT %1:%2").arg(m_host).arg(m_port));
    m_socket->connectToHost(m_host, m_port);
}

void MqttClient::socketConnected()
{
    m_lastReceive.restart();
    m_lastSend.restart();
    sendConnect();
}

void MqttClient::socketDisconnected()
{
    const bool wasStopping = m_stopping;
    m_state = wasStopping ? Stopped : Connecting;
    setOnline(false);
    if (!wasStopping) scheduleReconnect();
}

void MqttClient::socketReadyRead()
{
    m_input += m_socket->readAll();
    m_lastReceive.restart();
    parsePackets();
}

void MqttClient::socketError()
{
    emit logMessage(QStringLiteral("MQTT 网络错误：%1").arg(m_socket->errorString()));
    if (m_socket->state() == QAbstractSocket::UnconnectedState && !m_stopping)
        scheduleReconnect();
}

void MqttClient::maintenanceTick()
{
    if (m_stopping)
        return;
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        if (m_lastReceive.isValid() && m_lastReceive.elapsed() > 45000) {
            emit logMessage(QStringLiteral("MQTT 45 秒未收到服务端数据，重新连接"));
            m_socket->abort();
            return;
        }
        if (m_lastSend.isValid() && m_lastSend.elapsed() > 15000)
            sendPacket(0xC0, QByteArray());
    } else if (!m_reconnectTimer->isActive()) {
        scheduleReconnect();
    }
}

QByteArray MqttClient::encodeRemainingLength(int length)
{
    QByteArray result;
    do {
        quint8 digit = quint8(length % 128);
        length /= 128;
        if (length > 0) digit |= 0x80;
        result.append(char(digit));
    } while (length > 0);
    return result;
}

void MqttClient::appendUtf8(QByteArray *target, const QByteArray &value)
{
    target->append(char((value.size() >> 8) & 0xff));
    target->append(char(value.size() & 0xff));
    target->append(value);
}

bool MqttClient::decodeRemainingLength(const QByteArray &buffer, int *value, int *bytesUsed)
{
    int multiplier = 1;
    int decoded = 0;
    for (int i = 1; i < buffer.size() && i <= 4; ++i) {
        const quint8 digit = quint8(buffer.at(i));
        decoded += (digit & 0x7f) * multiplier;
        if ((digit & 0x80) == 0) {
            *value = decoded;
            *bytesUsed = i;
            return true;
        }
        multiplier *= 128;
    }
    return false;
}

quint16 MqttClient::nextPacketId()
{
    ++m_packetId;
    if (m_packetId == 0) ++m_packetId;
    return m_packetId;
}

void MqttClient::sendPacket(quint8 header, const QByteArray &body)
{
    if (m_socket->state() != QAbstractSocket::ConnectedState)
        return;
    QByteArray packet;
    packet.append(char(header));
    packet.append(encodeRemainingLength(body.size()));
    packet.append(body);
    m_socket->write(packet);
    m_lastSend.restart();
}

void MqttClient::sendConnect()
{
    QByteArray body;
    appendUtf8(&body, QByteArrayLiteral("MQTT"));
    body.append(char(4));       // MQTT 3.1.1
    body.append(char(0x02));    // Clean Session
    body.append(char(0));
    body.append(char(30));      // keepAlive = 30 s
    appendUtf8(&body, m_clientId.toUtf8());
    m_state = AwaitingConnAck;
    sendPacket(0x10, body);
}

void MqttClient::sendSubscribe()
{
    QByteArray body;
    const quint16 id = nextPacketId();
    body.append(char(id >> 8));
    body.append(char(id & 0xff));
    appendUtf8(&body, commandTopic().toUtf8());
    body.append(char(1));
    m_state = AwaitingSubAck;
    sendPacket(0x82, body);
}

bool MqttClient::publishTelemetry(const QByteArray &json)
{
    if (m_state != Ready || json.isEmpty())
        return false;
    QByteArray body;
    appendUtf8(&body, telemetryTopic().toUtf8());
    body.append(json);
    sendPacket(0x30, body);     // QoS 0, retain=false
    return true;
}

void MqttClient::parsePackets()
{
    while (m_input.size() >= 2) {
        int remaining = 0;
        int lengthBytes = 0;
        if (!decodeRemainingLength(m_input, &remaining, &lengthBytes)) {
            if (m_input.size() > 5) protocolError(QStringLiteral("MQTT Remaining Length 无效"));
            return;
        }
        const int headerLength = 1 + lengthBytes;
        if (remaining < 0 || remaining > 1024 * 1024) {
            protocolError(QStringLiteral("MQTT 数据包过大"));
            return;
        }
        if (m_input.size() < headerLength + remaining)
            return;
        const quint8 header = quint8(m_input.at(0));
        const QByteArray body = m_input.mid(headerLength, remaining);
        m_input.remove(0, headerLength + remaining);
        processPacket(header, body);
    }
}

void MqttClient::processPacket(quint8 header, const QByteArray &body)
{
    const int type = header >> 4;
    if (type == 2) { // CONNACK
        if (body.size() != 2 || quint8(body.at(1)) != 0) {
            const int code = body.size() >= 2 ? quint8(body.at(1)) : -1;
            protocolError(QStringLiteral("MQTT CONNECT 被拒绝，代码 %1").arg(code));
            return;
        }
        emit logMessage(QStringLiteral("MQTT 已连接，订阅 %1").arg(commandTopic()));
        sendSubscribe();
        return;
    }
    if (type == 9) { // SUBACK
        if (body.size() < 3 || quint8(body.at(2)) == 0x80) {
            protocolError(QStringLiteral("MQTT 命令主题订阅失败"));
            return;
        }
        m_state = Ready;
        m_reconnectSeconds = 1;
        setOnline(true);
        emit logMessage(QStringLiteral("MQTT 命令主题订阅成功"));
        return;
    }
    if (type == 3) { // PUBLISH
        if (body.size() < 2) {
            protocolError(QStringLiteral("MQTT PUBLISH 数据包过短"));
            return;
        }
        const int topicLength = (quint8(body.at(0)) << 8) | quint8(body.at(1));
        if (topicLength <= 0 || body.size() < 2 + topicLength) {
            protocolError(QStringLiteral("MQTT PUBLISH Topic 无效"));
            return;
        }
        int offset = 2 + topicLength;
        const int qos = (header >> 1) & 0x03;
        quint16 incomingPacketId = 0;
        if (qos > 0) {
            if (body.size() < offset + 2) {
                protocolError(QStringLiteral("MQTT QoS 数据包缺少 Packet ID"));
                return;
            }
            incomingPacketId = (quint8(body.at(offset)) << 8) | quint8(body.at(offset + 1));
            offset += 2;
        }
        const QString topic = QString::fromUtf8(body.constData() + 2, topicLength);
        const QByteArray payload = body.mid(offset);
        if (qos == 1) {
            QByteArray ack;
            ack.append(char(incomingPacketId >> 8));
            ack.append(char(incomingPacketId & 0xff));
            sendPacket(0x40, ack);
        }
        if (topic == commandTopic())
            emit messageReceived(topic, payload);
        else
            emit logMessage(QStringLiteral("忽略非命令主题：%1").arg(topic));
        return;
    }
    if (type == 13) // PINGRESP
        return;
    if (type == 14) {
        emit logMessage(QStringLiteral("MQTT 服务端主动断开连接"));
        m_socket->abort();
    }
}

void MqttClient::scheduleReconnect()
{
    if (m_stopping || m_reconnectTimer->isActive())
        return;
    const int seconds = qBound(1, m_reconnectSeconds, 60);
    emit logMessage(QStringLiteral("MQTT 将在 %1 秒后重连").arg(seconds));
    m_reconnectTimer->start(seconds * 1000);
    m_reconnectSeconds = qMin(seconds * 2, 60);
}

void MqttClient::setOnline(bool online)
{
    if (m_online == online)
        return;
    m_online = online;
    emit onlineChanged(online);
}

void MqttClient::protocolError(const QString &message)
{
    emit logMessage(message);
    m_socket->abort();
}
