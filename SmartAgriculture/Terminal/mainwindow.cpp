#include "mainwindow.h"

#include "mqttclient.h"
#include "sensorworker.h"
#include "sysfscontroller.h"
#include "canchannel.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSerialPort>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <stdio.h>

MainWindow::MainWindow(const AppConfig &config, QWidget *parent)
    : QMainWindow(parent),
      m_config(config),
      m_mqtt(new MqttClient(this)),
      m_sensors(new SensorWorker(config.aht20Device, config.lightInputDevice,
                                 config.telemetryIntervalMs, config.simulateSensors, this)),
      m_outputs(new SysfsController(config.fanSysfsPath, config.ledSysfsPath, this)),
      m_telemetryTimer(new QTimer(this)),
      m_temperatureLabel(0),
      m_humidityLabel(0),
      m_lightLabel(0),
      m_mqttLabel(0),
      m_deviceLabel(0),
      m_outputLabel(0),
      m_fanLevel(0),
      m_lightButton(0),
      m_log(0),
      m_temperature(0.0),
      m_humidity(0.0),
      m_light(0),
      m_temperatureValid(false),
      m_humidityValid(false),
      m_lightValid(false),
      m_updatingControls(false)
{
    setWindowTitle(QStringLiteral("ELF1 智能农业终端"));
    resize(1024, 650);

    QTabWidget *tabs = new QTabWidget(this);
    tabs->addTab(createDashboardPage(), QStringLiteral("监测与控制"));
    tabs->addTab(createSerialPage(), QStringLiteral("UART / RS-485"));
    tabs->addTab(createCanPage(), QStringLiteral("CAN"));
    setCentralWidget(tabs);

    connect(m_sensors, SIGNAL(sampleReady(double,double,int,bool,bool,bool)),
            this, SLOT(handleSensorSample(double,double,int,bool,bool,bool)));
    connect(m_sensors, SIGNAL(sensorStatus(QString)), this, SLOT(appendLog(QString)));
    connect(m_outputs, SIGNAL(stateChanged(int,bool)), this, SLOT(updateOutputState(int,bool)));
    connect(m_outputs, SIGNAL(operationFailed(QString)), this, SLOT(appendLog(QString)));
    connect(m_mqtt, SIGNAL(logMessage(QString)), this, SLOT(appendLog(QString)));
    connect(m_mqtt, SIGNAL(messageReceived(QString,QByteArray)),
            this, SLOT(handleMqttCommand(QString,QByteArray)));
    connect(m_mqtt, &MqttClient::onlineChanged, this, [this](bool online) {
        m_mqttLabel->setText(online ? QStringLiteral("在线") : QStringLiteral("离线 / 重连中"));
        m_mqttLabel->setStyleSheet(online ? QStringLiteral("color:#188038;font-weight:bold")
                                          : QStringLiteral("color:#c5221f;font-weight:bold"));
    });

    connect(m_telemetryTimer, SIGNAL(timeout()), this, SLOT(publishTelemetry()));
    m_telemetryTimer->setInterval(config.telemetryIntervalMs);
    m_telemetryTimer->start();

    QString refreshError;
    if (!m_outputs->refresh(&refreshError) && !refreshError.isEmpty())
        appendLog(refreshError);

    m_mqtt->configure(config.serverHost, config.mqttPort, config.deviceId);
    m_mqtt->start();
    m_sensors->start();
}

MainWindow::~MainWindow()
{
    m_sensors->requestStop();
    m_sensors->wait(3000);
    m_mqtt->stop();
}

QWidget *MainWindow::createDashboardPage()
{
    QWidget *page = new QWidget;
    QVBoxLayout *root = new QVBoxLayout(page);

    QGroupBox *statusBox = new QGroupBox(QStringLiteral("设备状态"));
    QGridLayout *status = new QGridLayout(statusBox);
    m_deviceLabel = new QLabel(m_config.deviceId);
    m_mqttLabel = new QLabel(QStringLiteral("离线"));
    status->addWidget(new QLabel(QStringLiteral("设备 ID")), 0, 0);
    status->addWidget(m_deviceLabel, 0, 1);
    status->addWidget(new QLabel(QStringLiteral("MQTT")), 0, 2);
    status->addWidget(m_mqttLabel, 0, 3);
    status->addWidget(new QLabel(QStringLiteral("服务端")), 1, 0);
    status->addWidget(new QLabel(QStringLiteral("%1:%2").arg(m_config.serverHost).arg(m_config.mqttPort)), 1, 1, 1, 3);
    root->addWidget(statusBox);

    QGroupBox *sensorBox = new QGroupBox(QStringLiteral("实时环境数据"));
    QGridLayout *sensors = new QGridLayout(sensorBox);
    m_temperatureLabel = new QLabel(QStringLiteral("--.- °C"));
    m_humidityLabel = new QLabel(QStringLiteral("--.- %RH"));
    m_lightLabel = new QLabel(QStringLiteral("---- lux"));
    const QString valueStyle = QStringLiteral("font-size:28px;font-weight:bold;padding:18px;");
    m_temperatureLabel->setStyleSheet(valueStyle);
    m_humidityLabel->setStyleSheet(valueStyle);
    m_lightLabel->setStyleSheet(valueStyle);
    sensors->addWidget(new QLabel(QStringLiteral("温度")), 0, 0);
    sensors->addWidget(new QLabel(QStringLiteral("湿度")), 0, 1);
    sensors->addWidget(new QLabel(QStringLiteral("光照")), 0, 2);
    sensors->addWidget(m_temperatureLabel, 1, 0);
    sensors->addWidget(m_humidityLabel, 1, 1);
    sensors->addWidget(m_lightLabel, 1, 2);
    root->addWidget(sensorBox);

    QGroupBox *controlBox = new QGroupBox(QStringLiteral("本地与 MQTT 共用控制"));
    QHBoxLayout *controls = new QHBoxLayout(controlBox);
    m_lightButton = new QPushButton(QStringLiteral("打开 LED1"));
    m_lightButton->setCheckable(true);
    m_fanLevel = new QComboBox;
    for (int i = 0; i <= 4; ++i)
        m_fanLevel->addItem(QStringLiteral("风扇 %1 档").arg(i), i);
    m_outputLabel = new QLabel(QStringLiteral("状态读取中"));
    controls->addWidget(m_lightButton);
    controls->addWidget(m_fanLevel);
    controls->addWidget(m_outputLabel, 1);
    root->addWidget(controlBox);

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(500);
    root->addWidget(new QLabel(QStringLiteral("运行日志")));
    root->addWidget(m_log, 1);

    connect(m_lightButton, &QPushButton::toggled, this, [this](bool checked) {
        if (!m_updatingControls) m_outputs->setLightOn(checked);
    });
    connect(m_fanLevel, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        if (!m_updatingControls) m_outputs->setFanLevel(m_fanLevel->itemData(index).toInt());
    });
    return page;
}

QWidget *MainWindow::createSerialGroup(const QString &title,
                                       const QString &defaultDevice,
                                       SerialWidgets *widgets)
{
    QGroupBox *box = new QGroupBox(title);
    QVBoxLayout *root = new QVBoxLayout(box);
    QHBoxLayout *settings = new QHBoxLayout;
    widgets->port = new QSerialPort(this);
    widgets->device = new QLineEdit(defaultDevice);
    widgets->baud = new QComboBox;
    foreach (int baud, QList<int>() << 9600 << 19200 << 38400 << 57600 << 115200)
        widgets->baud->addItem(QString::number(baud), baud);
    widgets->baud->setCurrentText(QStringLiteral("115200"));
    widgets->openButton = new QPushButton(QStringLiteral("打开"));
    settings->addWidget(widgets->device, 1);
    settings->addWidget(widgets->baud);
    settings->addWidget(widgets->openButton);
    root->addLayout(settings);

    widgets->receiveEdit = new QPlainTextEdit;
    widgets->receiveEdit->setReadOnly(true);
    widgets->receiveEdit->setMaximumBlockCount(300);
    root->addWidget(widgets->receiveEdit, 1);
    QHBoxLayout *sender = new QHBoxLayout;
    widgets->sendEdit = new QLineEdit;
    QPushButton *sendButton = new QPushButton(QStringLiteral("发送 UTF-8"));
    sender->addWidget(widgets->sendEdit, 1);
    sender->addWidget(sendButton);
    root->addLayout(sender);

    connect(widgets->openButton, &QPushButton::clicked, this, [this, widgets]() { toggleSerial(widgets); });
    connect(sendButton, &QPushButton::clicked, this, [this, widgets]() { sendSerial(widgets); });
    connect(widgets->sendEdit, &QLineEdit::returnPressed, this, [this, widgets]() { sendSerial(widgets); });
    connect(widgets->port, &QSerialPort::readyRead, this, [this, widgets]() { receiveSerial(widgets); });
    connect(widgets->port, static_cast<void(QSerialPort::*)(QSerialPort::SerialPortError)>(&QSerialPort::error),
            this, [this, widgets](QSerialPort::SerialPortError error) {
        if (error != QSerialPort::NoError)
            appendLog(QStringLiteral("串口 %1：%2").arg(widgets->device->text(), widgets->port->errorString()));
    });
    return box;
}

QWidget *MainWindow::createSerialPage()
{
    QWidget *page = new QWidget;
    QHBoxLayout *layout = new QHBoxLayout(page);
    layout->addWidget(createSerialGroup(QStringLiteral("端口 A"), QStringLiteral("/dev/ttymxc1"), &m_serial1));
    layout->addWidget(createSerialGroup(QStringLiteral("端口 B"), QStringLiteral("/dev/ttymxc2"), &m_serial2));
    return page;
}

QWidget *MainWindow::createCanPage()
{
    QWidget *page = new QWidget;
    QVBoxLayout *root = new QVBoxLayout(page);
    QLabel *notice = new QLabel(QStringLiteral(
        "单板双通道测试：CAN0_H→CAN1_H、CAN0_L→CAN1_L，并按总线要求接 120Ω 终端电阻。"
        "两个通道必须选择相同波特率。数据输入为十六进制字节，最多 8 字节。"));
    notice->setWordWrap(true);
    root->addWidget(notice);
    QHBoxLayout *channels = new QHBoxLayout;
    channels->addWidget(createCanGroup(QStringLiteral("can0"), &m_can0));
    channels->addWidget(createCanGroup(QStringLiteral("can1"), &m_can1));
    root->addLayout(channels, 1);
    return page;
}

QWidget *MainWindow::createCanGroup(const QString &interfaceName, CanWidgets *widgets)
{
    QGroupBox *box = new QGroupBox(interfaceName);
    QVBoxLayout *root = new QVBoxLayout(box);
    QHBoxLayout *settings = new QHBoxLayout;
    widgets->channel = new CanChannel(this);
    widgets->bitrate = new QComboBox;
    foreach (int bitrate, QList<int>() << 100000 << 125000 << 250000 << 500000 << 1000000)
        widgets->bitrate->addItem(QString::number(bitrate), bitrate);
    widgets->bitrate->setCurrentText(QStringLiteral("500000"));
    widgets->openButton = new QPushButton(QStringLiteral("启动"));
    settings->addWidget(new QLabel(QStringLiteral("bitrate")));
    settings->addWidget(widgets->bitrate, 1);
    settings->addWidget(widgets->openButton);
    root->addLayout(settings);

    widgets->receiveEdit = new QPlainTextEdit;
    widgets->receiveEdit->setReadOnly(true);
    widgets->receiveEdit->setMaximumBlockCount(300);
    root->addWidget(widgets->receiveEdit, 1);

    QHBoxLayout *sender = new QHBoxLayout;
    widgets->idEdit = new QLineEdit(QStringLiteral("123"));
    widgets->idEdit->setMaximumWidth(80);
    widgets->dataEdit = new QLineEdit(QStringLiteral("01 02 03 04"));
    QPushButton *sendButton = new QPushButton(QStringLiteral("发送"));
    sender->addWidget(new QLabel(QStringLiteral("ID")));
    sender->addWidget(widgets->idEdit);
    sender->addWidget(widgets->dataEdit, 1);
    sender->addWidget(sendButton);
    root->addLayout(sender);

    connect(widgets->openButton, &QPushButton::clicked, this,
            [this, interfaceName, widgets]() { toggleCan(interfaceName, widgets); });
    connect(sendButton, &QPushButton::clicked, this, [this, widgets]() { sendCan(widgets); });
    connect(widgets->channel, &CanChannel::errorMessage, this, &MainWindow::appendLog);
    connect(widgets->channel, &CanChannel::frameReceived, this,
            [this, widgets](const QString &, quint32 identifier, const QByteArray &data) {
        receiveCan(widgets, identifier, data);
    });
    return box;
}

void MainWindow::toggleCan(const QString &interfaceName, CanWidgets *widgets)
{
    if (widgets->channel->isOpen()) {
        widgets->channel->closeChannel();
        widgets->openButton->setText(QStringLiteral("启动"));
        return;
    }
    if (widgets->channel->openChannel(interfaceName, widgets->bitrate->currentData().toInt())) {
        widgets->openButton->setText(QStringLiteral("停止"));
        appendLog(QStringLiteral("%1 已启动，bitrate=%2")
                  .arg(interfaceName).arg(widgets->bitrate->currentData().toInt()));
    }
}

QByteArray MainWindow::parseHexBytes(const QString &text, bool *ok)
{
    QString compact = text;
    compact.remove(QRegExp(QStringLiteral("[\\s,:-]")));
    if (compact.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        compact.remove(0, 2);
    if (compact.size() % 2 != 0 || compact.size() > 16) {
        if (ok) *ok = false;
        return QByteArray();
    }
    QByteArray result;
    for (int i = 0; i < compact.size(); i += 2) {
        bool byteOk = false;
        const int value = compact.mid(i, 2).toInt(&byteOk, 16);
        if (!byteOk) {
            if (ok) *ok = false;
            return QByteArray();
        }
        result.append(char(value));
    }
    if (ok) *ok = true;
    return result;
}

QString MainWindow::formatHexBytes(const QByteArray &data)
{
    QByteArray hex = data.toHex().toUpper();
    for (int i = hex.size() - 2; i > 0; i -= 2)
        hex.insert(i, ' ');
    return QString::fromLatin1(hex);
}

void MainWindow::sendCan(CanWidgets *widgets)
{
    bool idOk = false;
    const quint32 identifier = widgets->idEdit->text().trimmed().toUInt(&idOk, 16);
    bool dataOk = false;
    const QByteArray data = parseHexBytes(widgets->dataEdit->text(), &dataOk);
    if (!idOk || identifier > 0x7ff || !dataOk) {
        appendLog(QStringLiteral("CAN 输入无效：ID 应为 000～7FF，数据最多 8 个十六进制字节"));
        return;
    }
    if (widgets->channel->sendFrame(identifier, data))
        appendLog(QStringLiteral("%1 TX ID=%2 DATA=%3")
                  .arg(widgets->channel->interfaceName())
                  .arg(identifier, 3, 16, QLatin1Char('0'))
                  .arg(formatHexBytes(data)));
}

void MainWindow::receiveCan(CanWidgets *widgets, quint32 identifier, const QByteArray &data)
{
    widgets->receiveEdit->appendPlainText(QStringLiteral("%1  ID=%2  DATA=%3")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")))
        .arg(identifier, 3, 16, QLatin1Char('0'))
        .arg(formatHexBytes(data)));
}

void MainWindow::toggleSerial(SerialWidgets *widgets)
{
    if (widgets->port->isOpen()) {
        widgets->port->close();
        widgets->openButton->setText(QStringLiteral("打开"));
        return;
    }
    widgets->port->setPortName(widgets->device->text().trimmed());
    widgets->port->setBaudRate(widgets->baud->currentData().toInt());
    widgets->port->setDataBits(QSerialPort::Data8);
    widgets->port->setParity(QSerialPort::NoParity);
    widgets->port->setStopBits(QSerialPort::OneStop);
    widgets->port->setFlowControl(QSerialPort::NoFlowControl);
    if (!widgets->port->open(QIODevice::ReadWrite)) {
        appendLog(QStringLiteral("串口打开失败：%1").arg(widgets->port->errorString()));
        return;
    }
    widgets->openButton->setText(QStringLiteral("关闭"));
}

void MainWindow::sendSerial(SerialWidgets *widgets)
{
    if (!widgets->port->isOpen()) {
        appendLog(QStringLiteral("请先打开串口 %1").arg(widgets->device->text()));
        return;
    }
    const QByteArray data = widgets->sendEdit->text().toUtf8();
    if (!data.isEmpty()) widgets->port->write(data);
}

void MainWindow::receiveSerial(SerialWidgets *widgets)
{
    const QByteArray data = widgets->port->readAll();
    if (!data.isEmpty())
        widgets->receiveEdit->appendPlainText(QStringLiteral("RX %1  %2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                 QString::fromUtf8(data)));
}

void MainWindow::handleSensorSample(double temperature, double humidity, int light,
                                    bool temperatureValid, bool humidityValid, bool lightValid)
{
    m_temperature = temperature;
    m_humidity = humidity;
    m_light = light;
    m_temperatureValid = temperatureValid;
    m_humidityValid = humidityValid;
    m_lightValid = lightValid;
    updateValueLabel(m_temperatureLabel, temperatureValid ? QStringLiteral("%1 °C").arg(temperature, 0, 'f', 1) : QStringLiteral("无效"), temperatureValid);
    updateValueLabel(m_humidityLabel, humidityValid ? QStringLiteral("%1 %RH").arg(humidity, 0, 'f', 1) : QStringLiteral("无效"), humidityValid);
    updateValueLabel(m_lightLabel, lightValid ? QStringLiteral("%1 lux").arg(light) : QStringLiteral("无效"), lightValid);
    appendLog(QStringLiteral("采集 T=%1(%2) H=%3(%4) L=%5(%6)")
              .arg(temperature, 0, 'f', 1).arg(temperatureValid ? QStringLiteral("ok") : QStringLiteral("bad"))
              .arg(humidity, 0, 'f', 1).arg(humidityValid ? QStringLiteral("ok") : QStringLiteral("bad"))
              .arg(light).arg(lightValid ? QStringLiteral("ok") : QStringLiteral("bad")));
}

void MainWindow::updateValueLabel(QLabel *label, const QString &text, bool valid)
{
    label->setText(text);
    label->setStyleSheet(QStringLiteral("font-size:28px;font-weight:bold;padding:18px;color:%1;")
                         .arg(valid ? QStringLiteral("#202124") : QStringLiteral("#c5221f")));
}

void MainWindow::handleMqttCommand(const QString &, const QByteArray &payload)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        appendLog(QStringLiteral("拒绝非法 MQTT JSON：%1").arg(parseError.errorString()));
        return;
    }
    const QJsonObject object = document.object();
    const QJsonValue typeValue = object.value(QStringLiteral("type"));
    const QJsonValue value = object.value(QStringLiteral("value"));
    if (!typeValue.isString()) {
        appendLog(QStringLiteral("拒绝 MQTT 命令：type 必须是字符串"));
        return;
    }
    const QString type = typeValue.toString();
    bool applied = false;
    if (type == QStringLiteral("light") && value.isBool())
        applied = m_outputs->setLightOn(value.toBool());
    else if (type == QStringLiteral("fan") && value.isBool())
        applied = m_outputs->setFanOn(value.toBool());
    else if (type == QStringLiteral("curtain"))
        appendLog(QStringLiteral("窗帘硬件未配置，命令未执行"));
    else
        appendLog(QStringLiteral("拒绝 MQTT 命令：类型或 value 数据类型不符合协议"));

    if (applied) {
        appendLog(QStringLiteral("MQTT 命令已执行：%1").arg(QString::fromUtf8(payload)));
        publishTelemetry();
    }
}

void MainWindow::publishTelemetry()
{
    if (!m_temperatureValid || !m_humidityValid || !m_lightValid) {
        appendLog(QStringLiteral("遥测未上报：传感器数据尚不完整"));
        return;
    }
    QJsonObject object;
    object.insert(QStringLiteral("temperature"), m_temperature);
    object.insert(QStringLiteral("humidity"), m_humidity);
    object.insert(QStringLiteral("light"), m_light);
    object.insert(QStringLiteral("lightOn"), m_outputs->lightOn());
    object.insert(QStringLiteral("fanOn"), m_outputs->fanOn());
    const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (m_mqtt->publishTelemetry(json))
        appendLog(QStringLiteral("遥测上报：%1").arg(QString::fromUtf8(json)));
}

void MainWindow::appendLog(const QString &message)
{
    const QString line = QStringLiteral("[%1] %2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), message);
    fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
    fflush(stderr);
    if (m_log) m_log->appendPlainText(line);
}

void MainWindow::updateOutputState(int fanLevel, bool lightOn)
{
    m_updatingControls = true;
    m_fanLevel->setCurrentIndex(m_fanLevel->findData(fanLevel));
    m_lightButton->setChecked(lightOn);
    m_lightButton->setText(lightOn ? QStringLiteral("关闭 LED1") : QStringLiteral("打开 LED1"));
    m_outputLabel->setText(QStringLiteral("LED1：%1　风扇：%2 档")
                           .arg(lightOn ? QStringLiteral("开") : QStringLiteral("关"))
                           .arg(fanLevel));
    m_updatingControls = false;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_telemetryTimer->stop();
    m_sensors->requestStop();
    m_sensors->wait(3000);
    m_mqtt->stop();
    event->accept();
}
