#ifndef MQTTCLIENT_H
#define MQTTCLIENT_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>

class QTcpSocket;
class QTimer;

class MqttClient : public QObject
{
    Q_OBJECT
public:
    explicit MqttClient(QObject *parent = 0);
    ~MqttClient();

    void configure(const QString &host, quint16 port, const QString &clientId);
    bool isOnline() const { return m_online; }
    QString telemetryTopic() const;
    QString commandTopic() const;

public slots:
    void start();
    void stop();
    bool publishTelemetry(const QByteArray &json);

signals:
    void onlineChanged(bool online);
    void messageReceived(const QString &topic, const QByteArray &payload);
    void logMessage(const QString &message);

private slots:
    void connectToBroker();
    void socketConnected();
    void socketDisconnected();
    void socketReadyRead();
    void socketError();
    void maintenanceTick();

private:
    enum State { Stopped, Connecting, AwaitingConnAck, AwaitingSubAck, Ready };

    static QByteArray encodeRemainingLength(int length);
    static void appendUtf8(QByteArray *target, const QByteArray &value);
    static bool decodeRemainingLength(const QByteArray &buffer, int *value, int *bytesUsed);
    quint16 nextPacketId();
    void sendPacket(quint8 header, const QByteArray &body);
    void sendConnect();
    void sendSubscribe();
    void parsePackets();
    void processPacket(quint8 header, const QByteArray &body);
    void scheduleReconnect();
    void setOnline(bool online);
    void protocolError(const QString &message);

    QTcpSocket *m_socket;
    QTimer *m_maintenanceTimer;
    QTimer *m_reconnectTimer;
    QString m_host;
    quint16 m_port;
    QString m_clientId;
    QByteArray m_input;
    quint16 m_packetId;
    int m_reconnectSeconds;
    bool m_online;
    bool m_stopping;
    State m_state;
    QElapsedTimer m_lastReceive;
    QElapsedTimer m_lastSend;
};

#endif

