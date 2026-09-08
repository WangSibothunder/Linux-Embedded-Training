#include "appconfig.h"

#include <QFileInfo>
#include <QRegExp>
#include <QSettings>

AppConfig::AppConfig()
    : serverHost(QStringLiteral("192.168.0.100")),
      mqttPort(1883),
      deviceId(QStringLiteral("elf1-001")),
      telemetryIntervalMs(2000),
      aht20Device(QStringLiteral("/dev/aht20")),
      fanSysfsPath(QStringLiteral("/sys/devices/platform/pwm-fan-self")),
      ledSysfsPath(QStringLiteral("/sys/class/leds/led1")),
      simulateSensors(false)
{
}

AppConfig AppConfig::load(const QString &path, QString *error)
{
    AppConfig result;
    if (!path.isEmpty() && QFileInfo(path).exists()) {
        QSettings settings(path, QSettings::IniFormat);
        result.serverHost = settings.value(QStringLiteral("mqtt/host"), result.serverHost).toString().trimmed();
        result.mqttPort = quint16(settings.value(QStringLiteral("mqtt/port"), result.mqttPort).toUInt());
        result.deviceId = settings.value(QStringLiteral("mqtt/deviceId"), result.deviceId).toString().trimmed();
        result.telemetryIntervalMs = settings.value(QStringLiteral("mqtt/telemetryIntervalMs"), result.telemetryIntervalMs).toInt();
        result.aht20Device = settings.value(QStringLiteral("hardware/aht20Device"), result.aht20Device).toString();
        result.lightInputDevice = settings.value(QStringLiteral("hardware/lightInputDevice"), result.lightInputDevice).toString();
        result.fanSysfsPath = settings.value(QStringLiteral("hardware/fanSysfsPath"), result.fanSysfsPath).toString();
        result.ledSysfsPath = settings.value(QStringLiteral("hardware/ledSysfsPath"), result.ledSysfsPath).toString();
        result.simulateSensors = settings.value(QStringLiteral("hardware/simulateSensors"), false).toBool();
    }

    result.telemetryIntervalMs = qBound(500, result.telemetryIntervalMs, 60000);
    if (!result.isValid(error))
        return AppConfig();
    return result;
}

bool AppConfig::isValid(QString *error) const
{
    if (serverHost.isEmpty()) {
        if (error) *error = QStringLiteral("MQTT 服务端地址不能为空");
        return false;
    }
    if (mqttPort == 0) {
        if (error) *error = QStringLiteral("MQTT 端口必须为 1～65535");
        return false;
    }
    QRegExp devicePattern(QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));
    if (!devicePattern.exactMatch(deviceId)) {
        if (error) *error = QStringLiteral("设备 ID 只能包含字母、数字、下划线和连字符，长度 1～64");
        return false;
    }
    return true;
}
