#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <QString>

struct AppConfig
{
    AppConfig();

    QString serverHost;
    quint16 mqttPort;
    QString deviceId;
    int telemetryIntervalMs;
    QString aht20Device;
    QString lightInputDevice;
    QString fanSysfsPath;
    QString ledSysfsPath;
    bool simulateSensors;

    static AppConfig load(const QString &path, QString *error = 0);
    bool isValid(QString *error = 0) const;
};

#endif
