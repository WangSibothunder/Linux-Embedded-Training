#ifndef SERIALSESSION_H
#define SERIALSESSION_H
#include <QObject>
#include <QSerialPort>
#include <QTimer>

struct SerialOptions {
    QString port;
    qint32 baud = 115200;
    QSerialPort::DataBits dataBits = QSerialPort::Data8;
    QSerialPort::Parity parity = QSerialPort::NoParity;
    QSerialPort::StopBits stopBits = QSerialPort::OneStop;
    QSerialPort::FlowControl flow = QSerialPort::NoFlowControl;
};

// Application-level adapter to the existing OS serial driver, NOT a kernel driver.
class SerialSession : public QObject {
    Q_OBJECT
public:
    explicit SerialSession(QObject *parent = nullptr);
    bool open(const SerialOptions &options);
    void close();
    bool send(const QByteArray &data);
    bool isOpen() const;
    qint64 pendingBytes() const;
    static const int MaxPayload = 65536;
    static const int MaxQueue = 262144;
signals:
    void openedChanged(bool opened);
    void received(const QByteArray &data);
    void transmitted(qint64 bytes);
    void pendingChanged(qint64 bytes);
    void failure(const QString &message);
private:
    void pump();
    void handleError(QSerialPort::SerialPortError error);
    QSerialPort *m_port;
    QTimer m_stallTimer;
    QByteArray m_pending;
    bool m_opening = false;
    bool m_pumping = false;
    bool m_errorQueued = false;
    quint64 m_generation = 0;
};
#endif
