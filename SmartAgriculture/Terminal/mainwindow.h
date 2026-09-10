#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "appconfig.h"

#include <QMainWindow>

class QLabel;
class QComboBox;
class QPlainTextEdit;
class QLineEdit;
class QPushButton;
class QSerialPort;
class QTimer;
class MqttClient;
class SensorWorker;
class SysfsController;
class CanChannel;
class SensorTrendWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const AppConfig &config, QWidget *parent = 0);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) Q_DECL_OVERRIDE;

private slots:
    void handleSensorSample(double temperature, double humidity, int light,
                            bool temperatureValid, bool humidityValid, bool lightValid);
    void handleMqttCommand(const QString &topic, const QByteArray &payload);
    void publishTelemetry();
    void appendLog(const QString &message);
    void updateOutputState(int fanLevel, bool lightOn);

private:
    struct SerialWidgets {
        QSerialPort *port;
        QLineEdit *device;
        QComboBox *baud;
        QPushButton *openButton;
        QLineEdit *sendEdit;
        QPlainTextEdit *receiveEdit;
    };
    struct CanWidgets {
        CanChannel *channel;
        QComboBox *bitrate;
        QPushButton *openButton;
        QLineEdit *idEdit;
        QLineEdit *dataEdit;
        QPlainTextEdit *receiveEdit;
    };

    QWidget *createDashboardPage();
    QWidget *createSerialPage();
    QWidget *createSerialGroup(const QString &title, const QString &defaultDevice, SerialWidgets *widgets);
    QWidget *createCanPage();
    void toggleSerial(SerialWidgets *widgets);
    void sendSerial(SerialWidgets *widgets);
    void receiveSerial(SerialWidgets *widgets);
    QWidget *createCanGroup(const QString &interfaceName, CanWidgets *widgets);
    void toggleCan(const QString &interfaceName, CanWidgets *widgets);
    void sendCan(CanWidgets *widgets);
    void receiveCan(CanWidgets *widgets, quint32 identifier, const QByteArray &data);
    static QByteArray parseHexBytes(const QString &text, bool *ok);
    static QString formatHexBytes(const QByteArray &data);
    void updateValueLabel(QLabel *label, const QString &text, bool valid);

    AppConfig m_config;
    MqttClient *m_mqtt;
    SensorWorker *m_sensors;
    SysfsController *m_outputs;
    QTimer *m_telemetryTimer;

    QLabel *m_temperatureLabel;
    QLabel *m_humidityLabel;
    QLabel *m_lightLabel;
    QLabel *m_mqttLabel;
    QLabel *m_deviceLabel;
    QLabel *m_outputLabel;
    QLabel *m_sampleTimeLabel;
    QLabel *m_sourceLabel;
    QComboBox *m_fanLevel;
    QPushButton *m_lightButton;
    QPlainTextEdit *m_log;
    SensorTrendWidget *m_trendWidget;
    SerialWidgets m_serial1;
    SerialWidgets m_serial2;
    CanWidgets m_can0;
    CanWidgets m_can1;

    double m_temperature;
    double m_humidity;
    int m_light;
    bool m_temperatureValid;
    bool m_humidityValid;
    bool m_lightValid;
    bool m_updatingControls;
};

#endif
