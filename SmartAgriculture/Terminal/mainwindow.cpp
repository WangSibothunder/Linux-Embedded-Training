#include "mainwindow.h"

#include "mqttclient.h"
#include "sensorworker.h"
#include "sysfscontroller.h"
#include "canchannel.h"

#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSerialPort>
#include <QSizePolicy>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>
#include <QtMath>
#include <stdio.h>

class SensorTrendWidget : public QWidget
{
public:
    explicit SensorTrendWidget(QWidget *parent = 0)
        : QWidget(parent)
    {
        setMinimumHeight(112);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void appendSample(double temperature, double humidity, double light,
                      bool temperatureValid, bool humidityValid, bool lightValid)
    {
        appendValue(&m_temperature, temperatureValid ? temperature : qQNaN());
        appendValue(&m_humidity, humidityValid ? humidity : qQNaN());
        appendValue(&m_light, lightValid ? light : qQNaN());
        update();
    }

protected:
    void paintEvent(QPaintEvent *) Q_DECL_OVERRIDE
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), QColor(QStringLiteral("#0f1d18")));

        const QRectF plot = QRectF(rect()).adjusted(16.0, 30.0, -16.0, -13.0);
        painter.setPen(QPen(QColor(QStringLiteral("#263d34")), 1.0));
        for (int i = 0; i <= 3; ++i) {
            const qreal y = plot.top() + plot.height() * i / 3.0;
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }

        painter.setFont(QFont(QStringLiteral("Sans Serif"), 9));
        drawLegend(&painter, 16, QStringLiteral("温度"), QColor(QStringLiteral("#ffb86b")));
        drawLegend(&painter, 92, QStringLiteral("湿度"), QColor(QStringLiteral("#63d5ff")));
        drawLegend(&painter, 168, QStringLiteral("光照"), QColor(QStringLiteral("#8ce99a")));

        if (m_temperature.isEmpty()) {
            painter.setPen(QColor(QStringLiteral("#789185")));
            painter.drawText(plot, Qt::AlignCenter, QStringLiteral("等待第一组传感器数据…"));
            return;
        }

        double lightMaximum = 1000.0;
        for (int i = 0; i < m_light.size(); ++i) {
            if (qIsFinite(m_light.at(i)))
                lightMaximum = qMax(lightMaximum, m_light.at(i) * 1.15);
        }
        drawSeries(&painter, m_temperature, plot, -10.0, 50.0, QColor(QStringLiteral("#ffb86b")));
        drawSeries(&painter, m_humidity, plot, 0.0, 100.0, QColor(QStringLiteral("#63d5ff")));
        drawSeries(&painter, m_light, plot, 0.0, lightMaximum, QColor(QStringLiteral("#8ce99a")));
    }

private:
    static void appendValue(QVector<double> *values, double value)
    {
        values->append(value);
        while (values->size() > 48)
            values->remove(0);
    }

    static void drawLegend(QPainter *painter, int x, const QString &text, const QColor &color)
    {
        painter->setPen(QPen(color, 3.0, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(x, 15, x + 18, 15);
        painter->setPen(QColor(QStringLiteral("#b9cdc4")));
        painter->drawText(x + 25, 20, text);
    }

    static void drawSeries(QPainter *painter, const QVector<double> &values,
                           const QRectF &plot, double minimum, double maximum,
                           const QColor &color)
    {
        if (values.size() < 2 || maximum <= minimum)
            return;
        QPainterPath path;
        bool hasSegment = false;
        for (int i = 0; i < values.size(); ++i) {
            const double value = values.at(i);
            if (!qIsFinite(value)) {
                hasSegment = false;
                continue;
            }
            const qreal x = plot.left() + plot.width() * i / qMax(1, values.size() - 1);
            const double ratio = qBound(0.0, (value - minimum) / (maximum - minimum), 1.0);
            const qreal y = plot.bottom() - plot.height() * ratio;
            if (!hasSegment) {
                path.moveTo(x, y);
                hasSegment = true;
            } else {
                path.lineTo(x, y);
            }
        }
        painter->setPen(QPen(color, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPath(path);
    }

    QVector<double> m_temperature;
    QVector<double> m_humidity;
    QVector<double> m_light;
};

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
      m_sampleTimeLabel(0),
      m_sourceLabel(0),
      m_fanLevel(0),
      m_lightButton(0),
      m_log(0),
      m_trendWidget(0),
      m_temperature(0.0),
      m_humidity(0.0),
      m_light(0),
      m_temperatureValid(false),
      m_humidityValid(false),
      m_lightValid(false),
      m_updatingControls(false)
{
    setWindowTitle(QStringLiteral("ELF1 智能农业终端"));
    resize(1024, 600);
    setMinimumSize(800, 480);
    setStyleSheet(QStringLiteral(
        "QMainWindow,QWidget{background:#0a1411;color:#e8f3ed;font-family:'Microsoft YaHei','WenQuanYi Micro Hei';font-size:14px;}"
        "QTabWidget::pane{border:0;background:#0a1411;}"
        "QTabBar::tab{background:#102019;color:#8da69a;border:1px solid #20382e;padding:10px 24px;margin-right:4px;}"
        "QTabBar::tab:selected{background:#17372a;color:#f1fff7;border-color:#3fb778;}"
        "QFrame#topBar,QFrame#metricCard,QFrame#controlCard{background:#102019;border:1px solid #20382e;border-radius:10px;}"
        "QLabel#eyebrow{color:#70d99a;font-size:11px;font-weight:bold;}"
        "QLabel#pageTitle{color:#f5fbf8;font-size:23px;font-weight:bold;}"
        "QLabel#muted{color:#8ca398;font-size:12px;}"
        "QLabel#metricTitle{color:#9bb0a6;font-size:13px;}"
        "QLabel#metricValue{color:#f5fbf8;font-size:30px;font-weight:bold;}"
        "QLabel#statusPill{background:#33241c;color:#ffba73;border:1px solid #6b452b;border-radius:12px;padding:5px 11px;font-weight:bold;}"
        "QGroupBox{background:#102019;border:1px solid #20382e;border-radius:9px;margin-top:12px;padding-top:10px;font-weight:bold;}"
        "QGroupBox::title{subcontrol-origin:margin;left:12px;padding:0 6px;color:#c8d8d0;}"
        "QPushButton{background:#183328;color:#e8f3ed;border:1px solid #315645;border-radius:7px;padding:8px 14px;}"
        "QPushButton:hover{border-color:#58d68d;background:#1d4031;}"
        "QPushButton:pressed{background:#10271d;}"
        "QPushButton:checked{background:#71402d;border-color:#e18b57;color:#fff5ea;}"
        "QComboBox,QLineEdit{background:#0c1914;color:#eef8f2;border:1px solid #315044;border-radius:6px;padding:7px;}"
        "QPlainTextEdit{background:#08100d;color:#b9d0c4;border:1px solid #20382e;border-radius:7px;padding:5px;font-family:monospace;font-size:12px;}"
    ));

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
        m_mqttLabel->setText(online ? QStringLiteral("● MQTT 在线") : QStringLiteral("● MQTT 重连中"));
        m_mqttLabel->setStyleSheet(online
            ? QStringLiteral("background:#123c2a;color:#66e49a;border:1px solid #2b8153;border-radius:12px;padding:5px 11px;font-weight:bold;")
            : QStringLiteral("background:#33241c;color:#ffba73;border:1px solid #6b452b;border-radius:12px;padding:5px 11px;font-weight:bold;"));
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
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(10);

    QFrame *topBar = new QFrame;
    topBar->setObjectName(QStringLiteral("topBar"));
    QHBoxLayout *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(16, 10, 16, 10);
    QVBoxLayout *titles = new QVBoxLayout;
    titles->setSpacing(1);
    QLabel *eyebrow = new QLabel(QStringLiteral("ELF1 · FIELD TERMINAL"));
    eyebrow->setObjectName(QStringLiteral("eyebrow"));
    QLabel *title = new QLabel(QStringLiteral("智慧农业实时监测"));
    title->setObjectName(QStringLiteral("pageTitle"));
    titles->addWidget(eyebrow);
    titles->addWidget(title);
    topLayout->addLayout(titles);
    topLayout->addStretch();

    m_deviceLabel = new QLabel(m_config.deviceId);
    m_deviceLabel->setText(QStringLiteral("设备 %1").arg(m_config.deviceId));
    m_deviceLabel->setObjectName(QStringLiteral("muted"));
    m_mqttLabel = new QLabel(QStringLiteral("● MQTT 连接中"));
    m_mqttLabel->setObjectName(QStringLiteral("statusPill"));
    topLayout->addWidget(m_deviceLabel);
    topLayout->addSpacing(12);
    topLayout->addWidget(m_mqttLabel);
    root->addWidget(topBar);

    QHBoxLayout *metrics = new QHBoxLayout;
    metrics->setSpacing(10);
    const QStringList metricTitles = QStringList()
        << QStringLiteral("空气温度") << QStringLiteral("相对湿度") << QStringLiteral("环境光照");
    QLabel **metricValues[] = { &m_temperatureLabel, &m_humidityLabel, &m_lightLabel };
    const QStringList metricDefaults = QStringList()
        << QStringLiteral("--.- °C") << QStringLiteral("--.- %RH") << QStringLiteral("---- lux");
    const QStringList metricHints = QStringList()
        << QStringLiteral("AHT20 · 实时") << QStringLiteral("AHT20 · 实时") << QStringLiteral("BH1726 · 实时");
    for (int i = 0; i < 3; ++i) {
        QFrame *card = new QFrame;
        card->setObjectName(QStringLiteral("metricCard"));
        QVBoxLayout *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(15, 10, 15, 10);
        cardLayout->setSpacing(2);
        QLabel *metricTitle = new QLabel(metricTitles.at(i));
        metricTitle->setObjectName(QStringLiteral("metricTitle"));
        *metricValues[i] = new QLabel(metricDefaults.at(i));
        (*metricValues[i])->setObjectName(QStringLiteral("metricValue"));
        QLabel *hint = new QLabel(metricHints.at(i));
        hint->setObjectName(QStringLiteral("muted"));
        cardLayout->addWidget(metricTitle);
        cardLayout->addWidget(*metricValues[i]);
        cardLayout->addWidget(hint);
        metrics->addWidget(card, 1);
    }
    root->addLayout(metrics);

    QHBoxLayout *middle = new QHBoxLayout;
    middle->setSpacing(10);
    QFrame *trendCard = new QFrame;
    trendCard->setObjectName(QStringLiteral("metricCard"));
    QVBoxLayout *trendLayout = new QVBoxLayout(trendCard);
    trendLayout->setContentsMargins(12, 9, 12, 10);
    trendLayout->setSpacing(5);
    QHBoxLayout *trendHeader = new QHBoxLayout;
    QLabel *trendTitle = new QLabel(QStringLiteral("最近 48 次采样趋势"));
    trendTitle->setStyleSheet(QStringLiteral("font-weight:bold;color:#dcebe4;"));
    m_sampleTimeLabel = new QLabel(QStringLiteral("等待采集"));
    m_sampleTimeLabel->setObjectName(QStringLiteral("muted"));
    trendHeader->addWidget(trendTitle);
    trendHeader->addStretch();
    trendHeader->addWidget(m_sampleTimeLabel);
    trendLayout->addLayout(trendHeader);
    m_trendWidget = new SensorTrendWidget;
    trendLayout->addWidget(m_trendWidget, 1);
    middle->addWidget(trendCard, 2);

    QFrame *controlCard = new QFrame;
    controlCard->setObjectName(QStringLiteral("controlCard"));
    QVBoxLayout *controls = new QVBoxLayout(controlCard);
    controls->setContentsMargins(15, 11, 15, 11);
    controls->setSpacing(8);
    QLabel *controlTitle = new QLabel(QStringLiteral("现场控制"));
    controlTitle->setStyleSheet(QStringLiteral("font-weight:bold;color:#dcebe4;"));
    controls->addWidget(controlTitle);
    m_sourceLabel = new QLabel(m_config.simulateSensors
        ? QStringLiteral("数据源 · 模拟传感器") : QStringLiteral("数据源 · 板载传感器"));
    m_sourceLabel->setObjectName(QStringLiteral("muted"));
    controls->addWidget(m_sourceLabel);
    m_lightButton = new QPushButton(QStringLiteral("打开 LED1"));
    m_lightButton->setCheckable(true);
    m_fanLevel = new QComboBox;
    for (int i = 0; i <= 4; ++i)
        m_fanLevel->addItem(QStringLiteral("风扇 %1 档").arg(i), i);
    m_outputLabel = new QLabel(QStringLiteral("状态读取中"));
    m_outputLabel->setObjectName(QStringLiteral("muted"));
    controls->addWidget(m_lightButton);
    controls->addWidget(m_fanLevel);
    controls->addStretch();
    controls->addWidget(m_outputLabel);
    middle->addWidget(controlCard, 1);
    root->addLayout(middle, 1);

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(500);
    m_log->setMaximumHeight(105);
    m_log->setPlaceholderText(QStringLiteral("采集、上报与远程控制事件将在这里显示"));
    root->addWidget(m_log);

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
    if (m_sampleTimeLabel)
        m_sampleTimeLabel->setText(QStringLiteral("最近采集 · %1")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))));
    if (m_trendWidget)
        m_trendWidget->appendSample(temperature, humidity, light,
                                    temperatureValid, humidityValid, lightValid);
    appendLog(QStringLiteral("采集 T=%1(%2) H=%3(%4) L=%5(%6)")
              .arg(temperature, 0, 'f', 1).arg(temperatureValid ? QStringLiteral("ok") : QStringLiteral("bad"))
              .arg(humidity, 0, 'f', 1).arg(humidityValid ? QStringLiteral("ok") : QStringLiteral("bad"))
              .arg(light).arg(lightValid ? QStringLiteral("ok") : QStringLiteral("bad")));
}

void MainWindow::updateValueLabel(QLabel *label, const QString &text, bool valid)
{
    label->setText(text);
    label->setStyleSheet(QStringLiteral("font-size:30px;font-weight:bold;color:%1;")
                         .arg(valid ? QStringLiteral("#f5fbf8") : QStringLiteral("#ff7f7f")));
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
    object.insert(QStringLiteral("timestamp"), QDateTime::currentMSecsSinceEpoch());
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
