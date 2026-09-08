#include "sysfscontroller.h"

#include <QDir>
#include <QFile>

SysfsController::SysfsController(const QString &fanPath,
                                 const QString &ledPath,
                                 QObject *parent)
    : QObject(parent),
      m_fanPath(fanPath),
      m_ledPath(ledPath),
      m_fanLevel(0),
      m_lastNonZeroFanLevel(2),
      m_lightOn(false)
{
}

bool SysfsController::fanAvailable() const
{
    return QFile::exists(QDir(m_fanPath).filePath(QStringLiteral("fan_level")));
}

bool SysfsController::lightAvailable() const
{
    return QFile::exists(QDir(m_ledPath).filePath(QStringLiteral("brightness")));
}

bool SysfsController::readInteger(const QString &path, int *value, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("无法读取 %1：%2").arg(path, file.errorString());
        return false;
    }
    bool ok = false;
    const int parsed = QString::fromLatin1(file.readAll()).trimmed().toInt(&ok);
    if (!ok) {
        if (error) *error = QStringLiteral("%1 返回的不是整数").arg(path);
        return false;
    }
    if (value) *value = parsed;
    return true;
}

bool SysfsController::writeInteger(const QString &path, int value, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("无法写入 %1：%2").arg(path, file.errorString());
        return false;
    }
    const QByteArray data = QByteArray::number(value);
    if (file.write(data) != data.size()) {
        if (error) *error = QStringLiteral("写入 %1 失败：%2").arg(path, file.errorString());
        return false;
    }
    return file.flush();
}

void SysfsController::fail(const QString &message)
{
    emit operationFailed(message);
}

bool SysfsController::refresh(QString *error)
{
    QString localError;
    int fan = 0;
    int light = 0;
    const bool fanOk = readInteger(QDir(m_fanPath).filePath(QStringLiteral("fan_level")), &fan, &localError);
    if (!fanOk && error) *error = localError;
    const bool lightOk = readInteger(QDir(m_ledPath).filePath(QStringLiteral("brightness")), &light, &localError);
    if (!lightOk && error && error->isEmpty()) *error = localError;

    if (fanOk) {
        m_fanLevel = qBound(0, fan, 4);
        if (m_fanLevel > 0) m_lastNonZeroFanLevel = m_fanLevel;
    }
    if (lightOk) m_lightOn = light > 0;
    if (fanOk || lightOk) emit stateChanged(m_fanLevel, m_lightOn);
    return fanOk && lightOk;
}

bool SysfsController::setFanLevel(int level)
{
    level = qBound(0, level, 4);
    QString error;
    if (!writeInteger(QDir(m_fanPath).filePath(QStringLiteral("fan_level")), level, &error)) {
        fail(error);
        return false;
    }
    m_fanLevel = level;
    if (level > 0) m_lastNonZeroFanLevel = level;
    emit stateChanged(m_fanLevel, m_lightOn);
    return true;
}

bool SysfsController::setFanOn(bool on)
{
    return setFanLevel(on ? qBound(1, m_lastNonZeroFanLevel, 4) : 0);
}

bool SysfsController::setLightOn(bool on)
{
    QString error;
    const QString trigger = QDir(m_ledPath).filePath(QStringLiteral("trigger"));
    if (QFile::exists(trigger)) {
        QFile triggerFile(trigger);
        if (triggerFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            triggerFile.write("none");
            triggerFile.flush();
        }
    }
    if (!writeInteger(QDir(m_ledPath).filePath(QStringLiteral("brightness")), on ? 255 : 0, &error)) {
        fail(error);
        return false;
    }
    m_lightOn = on;
    emit stateChanged(m_fanLevel, m_lightOn);
    return true;
}
