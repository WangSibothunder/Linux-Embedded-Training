#include "canchannel.h"

#include <QFileInfo>
#include <QProcess>
#include <QSocketNotifier>

#ifdef Q_OS_LINUX
#include <errno.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

CanChannel::CanChannel(QObject *parent)
    : QObject(parent), m_socket(-1), m_notifier(0)
{
}

CanChannel::~CanChannel()
{
    closeChannel();
}

bool CanChannel::runIp(const QStringList &arguments, bool reportError)
{
    QString program = QStringLiteral("/usr/local/sbin/ip");
    if (!QFileInfo(program).isExecutable())
        program = QStringLiteral("ip");
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(3000) || !process.waitForFinished(5000) || process.exitCode() != 0) {
        if (reportError) {
            const QString detail = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
            emit errorMessage(QStringLiteral("CAN 配置失败：%1 %2；%3")
                              .arg(program, arguments.join(QStringLiteral(" ")), detail));
        }
        return false;
    }
    return true;
}

bool CanChannel::openChannel(const QString &interfaceName, int bitrate)
{
    closeChannel();
#ifndef Q_OS_LINUX
    emit errorMessage(QStringLiteral("SocketCAN 仅支持 Linux"));
    return false;
#else
    const QString name = interfaceName.trimmed();
    if (name.isEmpty() || bitrate <= 0) {
        emit errorMessage(QStringLiteral("CAN 接口名或波特率无效"));
        return false;
    }
    runIp(QStringList() << QStringLiteral("link") << QStringLiteral("set") << name << QStringLiteral("down"), false);
    if (!runIp(QStringList() << QStringLiteral("link") << QStringLiteral("set") << name
               << QStringLiteral("type") << QStringLiteral("can")
               << QStringLiteral("bitrate") << QString::number(bitrate)))
        return false;
    if (!runIp(QStringList() << QStringLiteral("link") << QStringLiteral("set") << name << QStringLiteral("up")))
        return false;

    const int descriptor = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (descriptor < 0) {
        emit errorMessage(QStringLiteral("创建 CAN Socket 失败：%1").arg(QString::fromLocal8Bit(strerror(errno))));
        return false;
    }
    struct ifreq request;
    memset(&request, 0, sizeof(request));
    const QByteArray encodedName = name.toLatin1();
    if (encodedName.size() >= IFNAMSIZ) {
        ::close(descriptor);
        emit errorMessage(QStringLiteral("CAN 接口名过长"));
        return false;
    }
    strncpy(request.ifr_name, encodedName.constData(), IFNAMSIZ - 1);
    if (::ioctl(descriptor, SIOCGIFINDEX, &request) < 0) {
        ::close(descriptor);
        emit errorMessage(QStringLiteral("找不到 CAN 接口 %1").arg(name));
        return false;
    }
    struct sockaddr_can address;
    memset(&address, 0, sizeof(address));
    address.can_family = AF_CAN;
    address.can_ifindex = request.ifr_ifindex;
    if (::bind(descriptor, reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) < 0) {
        const QString detail = QString::fromLocal8Bit(strerror(errno));
        ::close(descriptor);
        emit errorMessage(QStringLiteral("绑定 CAN 接口 %1 失败：%2").arg(name, detail));
        return false;
    }

    m_socket = descriptor;
    m_interface = name;
    m_notifier = new QSocketNotifier(m_socket, QSocketNotifier::Read, this);
    connect(m_notifier, SIGNAL(activated(int)), this, SLOT(readFrames()));
    emit channelStateChanged(m_interface, true);
    return true;
#endif
}

void CanChannel::closeChannel()
{
    const QString oldName = m_interface;
    if (m_notifier) {
        m_notifier->setEnabled(false);
        delete m_notifier;
        m_notifier = 0;
    }
#ifdef Q_OS_LINUX
    if (m_socket >= 0) ::close(m_socket);
#endif
    m_socket = -1;
    m_interface.clear();
    if (!oldName.isEmpty()) {
        runIp(QStringList() << QStringLiteral("link") << QStringLiteral("set")
              << oldName << QStringLiteral("down"), false);
        emit channelStateChanged(oldName, false);
    }
}

bool CanChannel::sendFrame(quint32 identifier, const QByteArray &data)
{
#ifndef Q_OS_LINUX
    Q_UNUSED(identifier)
    Q_UNUSED(data)
    return false;
#else
    if (m_socket < 0 || data.size() > 8 || identifier > CAN_SFF_MASK) {
        emit errorMessage(QStringLiteral("CAN 发送参数无效：仅支持 11 位 ID 和最多 8 字节"));
        return false;
    }
    struct can_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.can_id = identifier;
    frame.can_dlc = quint8(data.size());
    if (!data.isEmpty()) memcpy(frame.data, data.constData(), size_t(data.size()));
    if (::write(m_socket, &frame, sizeof(frame)) != ssize_t(sizeof(frame))) {
        emit errorMessage(QStringLiteral("CAN 发送失败：%1").arg(QString::fromLocal8Bit(strerror(errno))));
        return false;
    }
    return true;
#endif
}

void CanChannel::readFrames()
{
#ifdef Q_OS_LINUX
    if (m_socket < 0) return;
    struct can_frame frame;
    while (::recv(m_socket, &frame, sizeof(frame), MSG_DONTWAIT) == ssize_t(sizeof(frame))) {
        const QByteArray data(reinterpret_cast<const char *>(frame.data), frame.can_dlc);
        emit frameReceived(m_interface, frame.can_id & CAN_SFF_MASK, data);
    }
#endif
}
