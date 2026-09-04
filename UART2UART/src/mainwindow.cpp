#include "mainwindow.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QSerialPortInfo>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    buildUi();
    refreshPorts();
    loadSettings();
    connect(&m_serial, &SerialSession::received, this, &MainWindow::onReceive);
    connect(&m_serial, &SerialSession::transmitted, this, [this](qint64 n) { m_txCount += quint64(n); updateCounts(); });
    connect(&m_serial, &SerialSession::pendingChanged, this, [this](qint64) { updateCounts(); });
    connect(&m_serial, &SerialSession::failure, this, [this](const QString &error) {
        m_repeat->setChecked(false);
        log(error, true);
    });
    connect(&m_serial, &SerialSession::openedChanged, this, [this](bool on) {
        if (!on) m_repeat->setChecked(false);
        m_decoder.reset();
        updateControls();
        emit connectionChanged(on);
    });
    connect(&m_repeatTimer, &QTimer::timeout, this, [this]() {
        if (!m_serial.send(m_repeatPayload)) m_repeat->setChecked(false);
    });
    m_renderTimer.setInterval(50);
    connect(&m_renderTimer, &QTimer::timeout, this, &MainWindow::renderReceive);
    m_renderTimer.start();
    updateControls();
    updateCounts();
    log(tr("就绪：两端参数须一致。文本使用 UTF-8；接收数据不会自动回发。"));
}

MainWindow::~MainWindow()
{
    saveSettings();
    m_serial.disconnect(this);
    m_serial.close();
}

void MainWindow::buildUi()
{
    setWindowTitle(tr("UART2UART · 双向串口通信"));
    resize(900, 580);
    setMinimumSize(760, 460);
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);
    setCentralWidget(central);

    auto *heading = new QHBoxLayout;
    auto *title = new QLabel(tr("UART2UART  /  双向串口通信"));
    title->setStyleSheet("font-size:18px; font-weight:600; color:#122f43;");
    m_state = new QLabel;
    heading->addWidget(title); heading->addStretch(); heading->addWidget(m_state);
    layout->addLayout(heading);

    auto *settings = new QGroupBox(tr("连接设置"));
    auto *grid = new QGridLayout(settings);
    grid->setContentsMargins(8, 12, 8, 6);
    m_portBox = new QComboBox; m_portBox->setEditable(true); m_portBox->setMinimumWidth(175);
    m_portBox->setObjectName("portBox");
    m_portBox->setToolTip(tr("可手动输入 /dev/ttymxcN、/dev/ttyUSB0、/dev/pts/N 或 COM3；刷新不会尝试打开端口。"));
    m_refresh = new QPushButton(tr("刷新端口"));
    m_open = new QPushButton(tr("打开串口")); m_open->setObjectName("openButton");
    m_open->setStyleSheet("background:#176c77;color:white;font-weight:bold;");
    grid->addWidget(new QLabel(tr("串口")), 0, 0);
    grid->addWidget(m_portBox, 0, 1, 1, 3);
    grid->addWidget(m_refresh, 0, 4); grid->addWidget(m_open, 0, 5);

    m_baudBox = new QComboBox; m_baudBox->setEditable(true);
    for (int baud : {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600})
        m_baudBox->addItem(QString::number(baud));
    m_baudBox->setCurrentText("115200");
    m_dataBox = new QComboBox;
    for (int bits = 5; bits <= 8; ++bits) m_dataBox->addItem(QString::number(bits), bits);
    m_dataBox->setCurrentIndex(3);
    m_parityBox = new QComboBox;
    m_parityBox->addItem(tr("无 None"), QSerialPort::NoParity);
    m_parityBox->addItem(tr("偶 Even"), QSerialPort::EvenParity);
    m_parityBox->addItem(tr("奇 Odd"), QSerialPort::OddParity);
    m_parityBox->addItem(tr("Mark"), QSerialPort::MarkParity);
    m_parityBox->addItem(tr("Space"), QSerialPort::SpaceParity);
    m_stopBox = new QComboBox;
    m_stopBox->addItem("1", QSerialPort::OneStop); m_stopBox->addItem("2", QSerialPort::TwoStop);
    m_flowBox = new QComboBox;
    m_flowBox->addItem(tr("无"), QSerialPort::NoFlowControl);
    m_flowBox->addItem("RTS/CTS", QSerialPort::HardwareControl);
    m_flowBox->addItem("XON/XOFF", QSerialPort::SoftwareControl);
    const QList<QPair<QString, QComboBox *>> fields = {
        {tr("波特率"), m_baudBox}, {tr("数据位"), m_dataBox}, {tr("校验"), m_parityBox},
        {tr("停止位"), m_stopBox}, {tr("流控"), m_flowBox}
    };
    for (int i = 0; i < fields.size(); ++i) {
        const int row = 1 + i / 3, col = (i % 3) * 2;
        grid->addWidget(new QLabel(fields[i].first), row, col);
        grid->addWidget(fields[i].second, row, col + 1);
    }
    layout->addWidget(settings);

    auto *receiveBar = new QHBoxLayout;
    receiveBar->addWidget(new QLabel(tr("接收显示")));
    m_receiveMode = new QComboBox;
    m_receiveMode->setObjectName("receiveMode");
    m_receiveMode->addItems({tr("文本 UTF-8"), tr("HEX" )});
    receiveBar->addWidget(m_receiveMode);
    m_autoScroll = new QCheckBox(tr("自动滚动")); m_autoScroll->setChecked(true);
    receiveBar->addWidget(m_autoScroll); receiveBar->addStretch();
    auto *save = new QPushButton(tr("保存接收 .bin"));
    auto *clear = new QPushButton(tr("清空接收"));
    clear->setObjectName("clearButton");
    receiveBar->addWidget(save); receiveBar->addWidget(clear);
    layout->addLayout(receiveBar);
    auto *tabs = new QTabWidget;
    m_receiveEdit = new QPlainTextEdit; m_receiveEdit->setReadOnly(true);
    m_receiveEdit->setObjectName("receiveEdit");
    m_receiveEdit->setPlaceholderText(tr("收到的数据将显示在此处，支持同时发送和接收。"));
    m_logEdit = new QPlainTextEdit; m_logEdit->setReadOnly(true);
    m_logEdit->document()->setMaximumBlockCount(200);
    tabs->addTab(m_receiveEdit, tr("接收数据")); tabs->addTab(m_logEdit, tr("运行记录"));
    layout->addWidget(tabs, 3);

    auto *sendBar = new QHBoxLayout;
    sendBar->addWidget(new QLabel(tr("发送内容")));
    m_sendMode = new QComboBox; m_sendMode->addItems({tr("文本 UTF-8"), tr("HEX")});
    m_sendMode->setObjectName("sendMode");
    m_lineEnding = new QComboBox;
    m_lineEnding->setObjectName("lineEnding");
    m_lineEnding->addItems({tr("不追加换行"), "LF (\\n)", "CR (\\r)", "CRLF (\\r\\n)"});
    sendBar->addWidget(m_sendMode); sendBar->addWidget(m_lineEnding); sendBar->addStretch();
    layout->addLayout(sendBar);
    m_sendEdit = new QPlainTextEdit; m_sendEdit->setObjectName("sendEdit");
    m_sendEdit->setPlaceholderText(tr("文本：Hello / 你好    HEX：01 A2 FF 00"));
    m_sendEdit->setMaximumHeight(100); m_sendEdit->setMinimumHeight(45);
    layout->addWidget(m_sendEdit, 1);
    auto *actions = new QHBoxLayout;
    m_repeat = new QCheckBox(tr("定时发送"));
    m_repeat->setObjectName("repeatBox");
    m_interval = new QSpinBox; m_interval->setRange(10, 3600000); m_interval->setValue(1000); m_interval->setSuffix(" ms");
    m_send = new QPushButton(tr("发送一次")); m_send->setObjectName("sendButton");
    actions->addWidget(m_repeat); actions->addWidget(m_interval);
    actions->addStretch(); actions->addWidget(m_send);
    layout->addLayout(actions);
    m_counts = new QLabel; layout->addWidget(m_counts);
    m_counts->setWordWrap(true);
    m_counts->setStyleSheet("font-size:11px;color:#405361;");
    statusBar()->setSizeGripEnabled(false);
    central->setStyleSheet("QGroupBox{border:1px solid #c6d4db;border-radius:5px;margin-top:7px;}"
                           "QGroupBox::title{subcontrol-origin:margin;left:10px;}"
                           "QPushButton{padding:5px 10px;}"
                           "QPlainTextEdit{background:#f7fafc;color:#183246;border:1px solid #c6d4db;}"
                           "QComboBox{min-height:22px;}");

    connect(m_refresh, &QPushButton::clicked, this, &MainWindow::refreshPorts);
    connect(m_open, &QPushButton::clicked, this, &MainWindow::togglePort);
    connect(m_send, &QPushButton::clicked, this, &MainWindow::sendOnce);
    connect(m_repeat, &QCheckBox::toggled, this, &MainWindow::startRepeat);
    connect(clear, &QPushButton::clicked, this, &MainWindow::clearReceive);
    connect(save, &QPushButton::clicked, this, &MainWindow::saveReceive);
    connect(m_receiveMode, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, [this](int) { m_dirty = true; renderReceive(); });
}

void MainWindow::refreshPorts()
{
    const QString previous = m_portBox->currentText();
    m_portBox->clear();
    for (const auto &info : QSerialPortInfo::availablePorts()) {
        const QString name = info.systemLocation().isEmpty() ? info.portName() : info.systemLocation();
        m_portBox->addItem(name);
        m_portBox->setItemData(m_portBox->count() - 1, info.description(), Qt::ToolTipRole);
    }
    if (!previous.isEmpty()) m_portBox->setEditText(previous);
    // Do not guess a board UART: the debug console must not be opened automatically.
    else m_portBox->setCurrentIndex(-1);
}

void MainWindow::togglePort()
{
    if (m_serial.isOpen()) {
        log(tr("关闭串口；丢弃本地待发 %1 字节，不会自动重发。").arg(m_serial.pendingBytes()));
        m_serial.close();
        return;
    }
    bool ok = false;
    const int baud = m_baudBox->currentText().toInt(&ok);
    if (!ok || baud <= 0 || baud > 4000000) { log(tr("波特率请输入 1～4000000 的整数，实际支持范围取决于设备。"), true); return; }
    SerialOptions o;
    o.port = m_portBox->currentText(); o.baud = baud;
    o.dataBits = QSerialPort::DataBits(m_dataBox->currentData().toInt());
    o.parity = QSerialPort::Parity(m_parityBox->currentData().toInt());
    o.stopBits = QSerialPort::StopBits(m_stopBox->currentData().toInt());
    o.flow = QSerialPort::FlowControl(m_flowBox->currentData().toInt());
    if (m_serial.open(o)) { saveSettings(); log(tr("已打开 %1，波特率 %2。发送计数不代表对端已确认收到。").arg(o.port).arg(o.baud)); }
}

bool MainWindow::payload(QByteArray *result)
{
    const QString text = m_sendEdit->toPlainText();
    // Bound conversion cost too, not just the serial queue.
    if (text.size() > SerialSession::MaxPayload * 3) { log(tr("发送编辑区内容过长。"), true); return false; }
    if (m_sendMode->currentIndex() == 1) {
        QString error;
        if (!ByteCodec::parseHex(text, result, &error)) { log(error, true); return false; }
    } else *result = text.toUtf8();
    *result += ByteCodec::terminator(m_lineEnding->currentIndex());
    if (result->isEmpty() || result->size() > SerialSession::MaxPayload) {
        log(tr("发送数据须为 1～65536 字节（包括换行）。"), true); return false;
    }
    return true;
}

void MainWindow::sendOnce()
{
    QByteArray bytes;
    if (payload(&bytes) && m_serial.send(bytes)) log(tr("已加入发送队列：%1 字节。").arg(bytes.size()));
}

void MainWindow::startRepeat(bool on)
{
    if (!on) {
        const bool wasActive = m_repeatTimer.isActive();
        m_repeatTimer.stop(); m_repeatPayload.clear(); updateControls();
        if (wasActive) log(tr("定时发送已停止；已入队的数据继续发送，关闭串口可丢弃本地待发数据。"));
        return;
    }
    if (!m_serial.isOpen() || !payload(&m_repeatPayload)) { m_repeat->setChecked(false); return; }
    const QByteArray first = m_repeatPayload;
    if (!m_serial.send(first) || !m_repeat->isChecked()) { m_repeat->setChecked(false); return; }
    m_repeatTimer.start(m_interval->value());
    log(tr("开始定时发送，间隔 %1 ms；内容已锁定，取消勾选即可停止。").arg(m_interval->value()));
    updateControls();
}

void MainWindow::updateControls()
{
    const bool open = m_serial.isOpen(), repeating = m_repeat->isChecked();
    for (QComboBox *box : {m_portBox, m_baudBox, m_dataBox, m_parityBox, m_stopBox, m_flowBox}) box->setEnabled(!open);
    m_refresh->setEnabled(!open);
    m_open->setText(open ? tr("关闭串口") : tr("打开串口"));
    m_state->setText(open ? tr("● 已连接") : tr("○ 未连接"));
    m_state->setStyleSheet(open ? "color:#12684e;font-weight:bold;" : "color:#657782;");
    m_send->setEnabled(open && !repeating); m_repeat->setEnabled(open);
    m_interval->setEnabled(!repeating); m_sendEdit->setReadOnly(repeating);
    m_sendMode->setEnabled(!repeating); m_lineEnding->setEnabled(!repeating);
}

void MainWindow::onReceive(const QByteArray &data)
{
    m_rxCount += quint64(data.size());
    m_rxBuffer.append(data);
    const int excess = m_rxBuffer.size() - 1024 * 1024;
    if (excess > 0) { m_rxBuffer.remove(0, excess); m_dropped += quint64(excess); }
    m_textTail += m_decoder.decode(data);
    if (m_textTail.size() > 65536) m_textTail = m_textTail.right(65536);
    m_dirty = true;
}

void MainWindow::renderReceive()
{
    if (!m_dirty) return;
    m_dirty = false;
    const int oldScroll = m_receiveEdit->verticalScrollBar()->value();
    const QString text = m_receiveMode->currentIndex() == 1
            ? ByteCodec::hex(m_rxBuffer.right(8192)) : m_textTail;
    m_receiveEdit->setPlainText(text);
    m_receiveEdit->verticalScrollBar()->setValue(m_autoScroll->isChecked()
        ? m_receiveEdit->verticalScrollBar()->maximum() : oldScroll);
    updateCounts();
}

void MainWindow::clearReceive()
{
    m_rxBuffer.clear(); m_textTail.clear(); m_decoder.reset();
    m_rxCount = 0; m_dropped = 0; m_dirty = true;
    renderReceive();
    log(tr("已清空本地接收显示和接收计数；未清空设备缓冲区。"));
}

void MainWindow::saveReceive()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("保存原始接收数据（最多最近 1 MiB）"),
        "uart-receive.bin", tr("二进制数据 (*.bin);;所有文件 (*)"));
    if (path.isEmpty()) return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(m_rxBuffer) != m_rxBuffer.size() || !file.commit()) {
        log(tr("保存失败：%1").arg(file.errorString()), true); return;
    }
    log(tr("已保存 %1 字节原始数据；更早丢弃 %2 字节。").arg(m_rxBuffer.size()).arg(m_dropped));
}

void MainWindow::log(const QString &text, bool error)
{
    m_logEdit->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss.zzz ") + (error ? "ERROR  " : "INFO  ") + text);
    statusBar()->showMessage(text);
    statusBar()->setToolTip(text);
}

void MainWindow::updateCounts()
{
    m_counts->setText(tr("RX %1 B  |  TX %2 B  |  待发 %3 B  |  已丢弃旧 RX %4 B  · 文本显示 64K 字符 / HEX 8KiB / 保存 1MiB")
        .arg(m_rxCount).arg(m_txCount).arg(m_serial.pendingBytes()).arg(m_dropped));
}

void MainWindow::loadSettings()
{
    QSettings s;
    if (s.contains("port")) m_portBox->setEditText(s.value("port").toString());
    m_baudBox->setCurrentText(s.value("baud", "115200").toString());
    const QList<QPair<QString, QComboBox *>> boxes = {
        {"data", m_dataBox}, {"parity", m_parityBox}, {"stop", m_stopBox}, {"flow", m_flowBox},
        {"sendMode", m_sendMode}, {"receiveMode", m_receiveMode}, {"lineEnding", m_lineEnding}
    };
    for (const auto &entry : boxes) {
        const int i = s.value(entry.first, entry.second->currentIndex()).toInt();
        if (i >= 0 && i < entry.second->count()) entry.second->setCurrentIndex(i);
    }
    m_interval->setValue(s.value("interval", 1000).toInt());
    // Never auto-open, auto-transmit or restore payloads on launch.
}

void MainWindow::saveSettings()
{
    QSettings s;
    s.setValue("port", m_portBox->currentText()); s.setValue("baud", m_baudBox->currentText());
    const QList<QPair<QString, QComboBox *>> boxes = {
        {"data", m_dataBox}, {"parity", m_parityBox}, {"stop", m_stopBox}, {"flow", m_flowBox},
        {"sendMode", m_sendMode}, {"receiveMode", m_receiveMode}, {"lineEnding", m_lineEnding}
    };
    for (const auto &entry : boxes) s.setValue(entry.first, entry.second->currentIndex());
    s.setValue("interval", m_interval->value());
}
