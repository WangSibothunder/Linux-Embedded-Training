#ifndef SENSORWORKER_H
#define SENSORWORKER_H

#include <QAtomicInt>
#include <QThread>

class SensorWorker : public QThread
{
    Q_OBJECT
public:
    SensorWorker(const QString &aht20Device,
                 const QString &lightInputDevice,
                 int intervalMs,
                 bool simulate,
                 QObject *parent = 0);
    ~SensorWorker();

    void requestStop();

signals:
    void sampleReady(double temperature, double humidity, int light,
                     bool temperatureValid, bool humidityValid, bool lightValid);
    void sensorStatus(const QString &message);

protected:
    void run() Q_DECL_OVERRIDE;

private:
    QString findLightInputDevice() const;
    void runSimulation();

    QString m_aht20Device;
    QString m_lightInputDevice;
    int m_intervalMs;
    bool m_simulate;
    QAtomicInt m_stop;
};

#endif

