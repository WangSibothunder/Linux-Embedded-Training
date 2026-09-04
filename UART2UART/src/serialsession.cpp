#include "serialsession.h"

SerialSession::SerialSession(QObject *parent) : QObject(parent), m_port(new QSerialPort(this))
{
    m_port->setReadBufferSize(65536);
    m_stallTimer.setSingleShot(true);
    m_stallTimer.setInterval(10000);
    connect(m_port, &QSerialPort::readyRead, this, [this]() {
        const QByteArray data = m_port->readAll();
        if (!data.isEmpty()) emit received(data);
    });
    connect(m_port, &QSerialPort::bytesWritten, this, [this](qint64 count) {
        if (count > 0) emit transmitted(count);
        pump();
        if (pendingBytes() > 0) m_stallTimer.start(); else m_stallTimer.stop();
        emit pendingChanged(pendingBytes());
    });
#if QT_VERSION >= QT_VERSION_CHECK(5, 8, 0)
    connect(m_port, &QSerialPort::errorOccurred, this, &SerialSession::handleError);
#else
    connect(m_port, static_cast<void(QSerialPort::*)(QSerialPort::SerialPortError)>(&QSerialPort::error),
            this, &SerialSession::handleError);
#endif
    connect(&m_stallTimer, &QTimer::timeout, this, [this]() {
        emit failure(tr("发送连续 10 秒无进展，已关闭串口。检查 CTS/流控、线缆和设备；未发送的数据不会自动重发。"));
        close();
    });
}

bool SerialSession::open(const SerialOptions &o)
{
    if (isOpen()) close();
    ++m_generation;
    m_errorQueued = false;
    if (o.port.trimmed().isEmpty() || o.baud <= 0) {
        emit failure(tr("请选择或输入串口，并填写有效波特率。"));
        return false;
    }
    m_opening = true;
    m_port->setPortName(o.port.trimmed());
    const bool ok = m_port->setBaudRate(o.baud) && m_port->setDataBits(o.dataBits)
            && m_port->setParity(o.parity) && m_port->setStopBits(o.stopBits)
            && m_port->setFlowControl(o.flow) && m_port->open(QIODevice::ReadWrite);
    const QString detail = m_port->errorString();
    m_opening = false;
    if (!ok) {
        if (m_port->isOpen()) m_port->close();
        emit failure(tr("打开 %1 失败：%2。检查端口名称、权限、是否被其他程序占用及参数是否受支持。").arg(o.port, detail));
        return false;
    }
    emit openedChanged(true);
    return true;
}

void SerialSession::close()
{
    ++m_generation;
    m_errorQueued = false;
    const bool wasOpen = isOpen();
    m_stallTimer.stop();
    m_pending.clear();
    if (wasOpen) m_port->close();
    emit pendingChanged(0);
    if (wasOpen) emit openedChanged(false);
}

bool SerialSession::isOpen() const { return m_port->isOpen(); }
qint64 SerialSession::pendingBytes() const { return m_pending.size() + m_port->bytesToWrite(); }

bool SerialSession::send(const QByteArray &data)
{
    if (!isOpen()) { emit failure(tr("串口尚未打开。")); return false; }
    if (data.isEmpty()) { emit failure(tr("发送内容为空。")); return false; }
    if (data.size() > MaxPayload) { emit failure(tr("单次发送最多 64 KiB（含追加的换行）。")); return false; }
    if (pendingBytes() + data.size() > MaxQueue) {
        emit failure(tr("发送队列已达到 256 KiB 上限，本次数据未加入队列。请降低发送频率。"));
        return false;
    }
    m_pending.append(data);
    pump();
    if (pendingBytes() > 0 && !m_stallTimer.isActive()) m_stallTimer.start();
    emit pendingChanged(pendingBytes());
    return true; // Accepted into local queue; NOT a remote delivery acknowledgement.
}

void SerialSession::pump()
{
    if (m_pumping || !isOpen() || m_pending.isEmpty()) return;
    m_pumping = true;
    const qint64 accepted = m_port->write(m_pending);
    if (accepted > 0) m_pending.remove(0, int(accepted));
    m_pumping = false;
    if (accepted < 0) handleError(QSerialPort::WriteError);
    else if (!m_pending.isEmpty()) QTimer::singleShot(10, this, [this]() { pump(); });
}

void SerialSession::handleError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError || m_opening || !isOpen() || m_errorQueued) return;
    const QString detail = m_port->errorString();
    const bool fatal = error == QSerialPort::ResourceError || error == QSerialPort::DeviceNotFoundError
            || error == QSerialPort::PermissionError || error == QSerialPort::ReadError
            || error == QSerialPort::WriteError;
    m_errorQueued = true;
    // Avoid closing QSerialPort re-entrantly from inside write()/readAll().
    const quint64 generation = m_generation;
    QTimer::singleShot(0, this, [this, detail, fatal, generation]() {
        if (generation != m_generation) return; // Ignore errors from an earlier connection.
        emit failure(tr("串口错误：%1%2").arg(detail, fatal ? tr("；连接已关闭。") : QString()));
        if (fatal) close();
        m_errorQueued = false;
    });
}
