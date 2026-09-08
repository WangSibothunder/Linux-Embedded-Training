#ifndef SYSFSCONTROLLER_H
#define SYSFSCONTROLLER_H

#include <QObject>
#include <QString>

class SysfsController : public QObject
{
    Q_OBJECT
public:
    explicit SysfsController(const QString &fanPath,
                             const QString &ledPath,
                             QObject *parent = 0);

    bool refresh(QString *error = 0);
    int fanLevel() const { return m_fanLevel; }
    bool fanOn() const { return m_fanLevel > 0; }
    bool lightOn() const { return m_lightOn; }
    bool fanAvailable() const;
    bool lightAvailable() const;

public slots:
    bool setFanLevel(int level);
    bool setFanOn(bool on);
    bool setLightOn(bool on);

signals:
    void stateChanged(int fanLevel, bool lightOn);
    void operationFailed(const QString &message);

private:
    static bool readInteger(const QString &path, int *value, QString *error);
    static bool writeInteger(const QString &path, int value, QString *error);
    void fail(const QString &message);

    QString m_fanPath;
    QString m_ledPath;
    int m_fanLevel;
    int m_lastNonZeroFanLevel;
    bool m_lightOn;
};

#endif

