#include "sensorworker.h"

#include <QFile>
#include <QRegExp>
#include <QtMath>

#ifdef Q_OS_LINUX
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>
#endif

static bool readBh1726LuxFromSysfs(int *lux)
{
    QFile rawFile(QStringLiteral("/sys/bus/i2c/devices/1-0029/als_data"));
    if (!rawFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QList<QByteArray> fields = rawFile.readAll().simplified().split(' ');
    if (fields.size() < 2)
        return false;
    bool data0Ok = false;
    bool data1Ok = false;
    const int data0 = fields.at(0).toInt(&data0Ok);
    const int data1 = fields.at(1).toInt(&data1Ok);
    if (!data0Ok || !data1Ok || data0 < 0 || data1 < 0)
        return false;

    static const int judge[] = {157, 261, 1121, 4910};
    static const int coefficient0[] = {159, 403, 256, 221};
    static const int coefficient1[] = {941, -615, -84, -45};
    int index = 0;
    while (index < 4 && qint64(data1) * 1000 >= qint64(judge[index]) * data0)
        ++index;
    if (index >= 4) {
        *lux = 0;
        return true;
    }

    // 板载驱动初始化为 DATA0/DATA1 x128、100 ms（TIMING=0xdb）。
    // 这里沿用驱动 bh1726_calculate_light() 的整数计算顺序。
    qint64 value = qint64(coefficient0[index]) * data0 / 128
                 + qint64(coefficient1[index]) * data1 / 128;
    if (value < 0) value = 0;
    const qint64 integrationTimeUs = qint64(28) * 964 * (256 - 0xdb) / 10;
    value = value * 102600 / integrationTimeUs / 1000;
    *lux = int(qBound<qint64>(0, value, 65535));
    return true;
}

SensorWorker::SensorWorker(const QString &aht20Device,
                           const QString &lightInputDevice,
                           int intervalMs,
                           bool simulate,
                           QObject *parent)
    : QThread(parent),
      m_aht20Device(aht20Device),
      m_lightInputDevice(lightInputDevice),
      m_intervalMs(qBound(500, intervalMs, 60000)),
      m_simulate(simulate),
      m_stop(0)
{
}

SensorWorker::~SensorWorker()
{
    requestStop();
    wait(3000);
}

void SensorWorker::requestStop()
{
    m_stop.storeRelease(1);
}

QString SensorWorker::findLightInputDevice() const
{
    if (!m_lightInputDevice.trimmed().isEmpty())
        return m_lightInputDevice;

    QFile devices(QStringLiteral("/proc/bus/input/devices"));
    if (!devices.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    const QString text = QString::fromLocal8Bit(devices.readAll());
    const QStringList blocks = text.split(QRegExp(QStringLiteral("\\n\\s*\\n")), QString::SkipEmptyParts);
    foreach (const QString &block, blocks) {
        if (!block.contains(QStringLiteral("Name=\"lightsensor\""), Qt::CaseInsensitive))
            continue;
        QRegExp eventPattern(QStringLiteral("\\bevent(\\d+)\\b"));
        if (eventPattern.indexIn(block) >= 0)
            return QStringLiteral("/dev/input/event%1").arg(eventPattern.cap(1));
    }
    return QString();
}

void SensorWorker::runSimulation()
{
    emit sensorStatus(QStringLiteral("传感器模拟模式已启用"));
    int tick = 0;
    while (!m_stop.loadAcquire()) {
        const double temperature = 24.0 + qSin(tick / 8.0) * 2.0;
        const double humidity = 58.0 + qCos(tick / 10.0) * 5.0;
        const int light = 320 + int(qSin(tick / 5.0) * 120.0);
        emit sampleReady(temperature, humidity, qMax(0, light), true, true, true);
        ++tick;
        msleep(static_cast<unsigned long>(m_intervalMs));
    }
}

void SensorWorker::run()
{
    m_stop.storeRelease(0);
    if (m_simulate) {
        runSimulation();
        return;
    }

#ifndef Q_OS_LINUX
    emit sensorStatus(QStringLiteral("真实传感器采集仅支持 Linux；请启用 simulateSensors"));
    return;
#else
    const QByteArray ahtPath = QFile::encodeName(m_aht20Device);
    const int ahtFd = ::open(ahtPath.constData(), O_RDWR);
    if (ahtFd < 0)
        emit sensorStatus(QStringLiteral("AHT20 打开失败：%1").arg(QString::fromLocal8Bit(strerror(errno))));
    else
        emit sensorStatus(QStringLiteral("AHT20 已连接：%1").arg(m_aht20Device));

    const QString lightPath = findLightInputDevice();
    const QString lightEnablePath = QStringLiteral("/sys/bus/i2c/devices/1-0029/enable_als_sensor");
    QFile enable(lightEnablePath);
    if (enable.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        enable.write("0");
        enable.flush();
        enable.close();
    }
    const QByteArray encodedLightPath = QFile::encodeName(lightPath);
    const int lightFd = lightPath.isEmpty() ? -1 : ::open(encodedLightPath.constData(), O_RDONLY | O_NONBLOCK);
    msleep(50);
    enable.setFileName(lightEnablePath);
    if (enable.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        enable.write("1");
        enable.flush();
        enable.close();
    }
    if (lightFd < 0)
        emit sensorStatus(QStringLiteral("BH1726 输入设备未找到或无法打开"));
    else
        emit sensorStatus(QStringLiteral("BH1726 已连接：%1").arg(lightPath));

    int lastLight = 0;
    bool lastLightValid = false;
    while (!m_stop.loadAcquire()) {
        double temperature = 0.0;
        double humidity = 0.0;
        bool temperatureValid = false;
        bool humidityValid = false;

        if (ahtFd >= 0) {
            unsigned int raw[2] = {0, 0};
            const ssize_t result = ::read(ahtFd, raw, sizeof(raw));
            // ELF1 教学驱动成功时返回 0，但已经填充 8 字节缓冲区。
            if (result == 0 || result == ssize_t(sizeof(raw))) {
                humidity = double(raw[0]) * 100.0 / 1048576.0;
                temperature = double(raw[1]) * 200.0 / 1048576.0 - 50.0;
                humidityValid = qIsFinite(humidity) && humidity >= 0.0 && humidity <= 100.0;
                temperatureValid = qIsFinite(temperature) && temperature >= -40.0 && temperature <= 125.0;
            }
        }

        int fallbackLight = 0;
        if (readBh1726LuxFromSysfs(&fallbackLight)) {
            lastLight = fallbackLight;
            lastLightValid = true;
        }

        const int waitSliceMs = 100;
        int remainingMs = m_intervalMs;
        while (!m_stop.loadAcquire() && remainingMs > 0) {
            if (lightFd >= 0) {
                struct pollfd descriptor;
                descriptor.fd = lightFd;
                descriptor.events = POLLIN;
                descriptor.revents = 0;
                const int pollResult = ::poll(&descriptor, 1, qMin(waitSliceMs, remainingMs));
                if (pollResult > 0 && (descriptor.revents & POLLIN)) {
                    struct input_event eventData;
                    while (::read(lightFd, &eventData, sizeof(eventData)) == ssize_t(sizeof(eventData))) {
                        if (eventData.type == EV_ABS && eventData.value >= 0) {
                            lastLight = eventData.value;
                            lastLightValid = true;
                        }
                    }
                }
            } else {
                msleep(static_cast<unsigned long>(qMin(waitSliceMs, remainingMs)));
            }
            remainingMs -= qMin(waitSliceMs, remainingMs);
        }

        emit sampleReady(temperature, humidity, lastLight,
                         temperatureValid, humidityValid, lastLightValid);
    }

    if (lightFd >= 0) ::close(lightFd);
    if (ahtFd >= 0) ::close(ahtFd);
#endif
}
