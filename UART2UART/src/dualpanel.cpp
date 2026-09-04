#include "dualpanel.h"
#include <QComboBox>
#include <QDateTime>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSerialPortInfo>
#include <QTextDocument>
#include <QUuid>
#include <QVBoxLayout>

DualPanel::DualPanel(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(5);
    auto *note = new QLabel(tr("ELF1 RS-485 双口互测：A1 接 A2，B1 接 B2。A/B 不是 TX/RX，禁止将同一接口 A、B 短接。"));
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *settings = new QGridLayout;
    for (int i = 0; i < 2; ++i) {
        m_portBoxes[i] = new QComboBox;
        m_portBoxes[i]->setEditable(true);
        m_portBoxes[i]->setMinimumContentsLength(16);
        m_portBoxes[i]->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_portBoxes[i]->setObjectName(QString("dualPort%1").arg(i));
        settings->addWidget(new QLabel(i == 0 ? tr("接口1 · A1/B1") : tr("接口2 · A2/B2")), i, 0);
        settings->addWidget(m_portBoxes[i], i, 1);
    }
    m_baud = new QComboBox;
    for (int b : {9600, 19200, 38400, 57600, 115200}) m_baud->addItem(QString::number(b), b);
    m_baud->setCurrentIndex(4);
    m_baud->setObjectName("dualBaud");
    settings->addWidget(new QLabel(tr("两端波特率")), 0, 2);
    settings->addWidget(m_baud, 0, 3);
    settings->addWidget(new QLabel(tr("8 数据位 / 无校验 / 1 停止位 / 无流控")), 1, 2, 1, 2);
    m_refresh = new QPushButton(tr("刷新"));
    m_open = new QPushButton(tr("打开两个端口"));
    m_open->setObjectName("dualOpen");
    settings->addWidget(m_refresh, 0, 4);
    settings->addWidget(m_open, 1, 4);
    layout->addLayout(settings);
    auto *bar = new QHBoxLayout;
    m_status = new QLabel;
    m_format = new QComboBox;
    m_format->addItems({tr("文本 UTF-8"), tr("HEX")});
    m_format->setObjectName("dualFormat");
    auto *clear = new QPushButton(tr("清空两侧接收"));
    clear->setObjectName("dualClear");
    bar->addWidget(m_status); bar->addStretch();
    bar->addWidget(new QLabel(tr("收发格式"))); bar->addWidget(m_format); bar->addWidget(clear);
    layout->addLayout(bar);
    auto *panes = new QHBoxLayout;
    for (int i = 0; i < 2; ++i) {
        auto *group = new QGroupBox(i == 0 ? tr("接口1 · 收到接口2的数据") : tr("接口2 · 收到接口1的数据"));
        auto *vbox = new QVBoxLayout(group);
        vbox->setContentsMargins(6, 10, 6, 6);
        vbox->setSpacing(4);
        m_rx[i] = new QPlainTextEdit;
        m_rx[i]->setReadOnly(true);
        m_rx[i]->setObjectName(QString("dualRx%1").arg(i));
        m_rx[i]->setPlaceholderText(tr("这里仅显示此端口真实收到的数据"));
        m_rx[i]->setMinimumHeight(55);
        vbox->addWidget(m_rx[i], 2);
        m_counts[i] = new QLabel;
        vbox->addWidget(m_counts[i]);
        m_tx[i] = new QPlainTextEdit;
        m_tx[i]->setObjectName(QString("dualTx%1").arg(i));
        m_tx[i]->setPlaceholderText(tr("输入发送内容；不自动追加换行"));
        m_tx[i]->setMinimumHeight(40); m_tx[i]->setMaximumHeight(75);
        vbox->addWidget(m_tx[i], 1);
        auto *actions = new QHBoxLayout;
        m_send[i] = new QPushButton(i == 0 ? tr("发送 1 → 2") : tr("发送 2 → 1"));
        m_send[i]->setObjectName(QString("dualSend%1").arg(i));
        m_verify[i] = new QPushButton(i == 0 ? tr("验证 1 → 2") : tr("验证 2 → 1"));
        m_verify[i]->setObjectName(QString("dualVerify%1").arg(i));
        m_verify[i]->setToolTip(tr("发送唯一测试字符串；只有另一端真实收到完整字符串才判定通过。"));
        actions->addWidget(m_send[i]); actions->addWidget(m_verify[i]);
        vbox->addLayout(actions);
        panes->addWidget(group, 1);
        m_ports[i] = new SerialSession(this);
        connect(m_ports[i], &SerialSession::received, this, [this, i](const QByteArray &b) { receive(i, b); });
        connect(m_ports[i], &SerialSession::transmitted, this, [this, i](qint64 n) {
            m_txCount[i] += quint64(n); m_dirty[i] = true;
        });
        connect(m_ports[i], &SerialSession::pendingChanged, this, [this, i](qint64) {
            m_dirty[i] = true; updateControls();
        });
        connect(m_ports[i], &SerialSession::failure, this, [this, i](const QString &message) {
            log(tr("接口%1：%2").arg(i + 1).arg(message));
            if (m_checkActive) finishCheck(false, tr("串口错误，验证中止。"));
        });
        connect(m_ports[i], &SerialSession::openedChanged, this, [this](bool open) {
            if (!open && !m_opening && !m_closing) closePorts();
        });
        connect(m_send[i], &QPushButton::clicked, this, [this, i]() { send(i, false); });
        connect(m_verify[i], &QPushButton::clicked, this, [this, i]() { send(i, true); });
    }
    layout->addLayout(panes, 1);
    m_checkResult = new QLabel(tr("尚未验证。先打开两个端口，再分别点击两个方向的“验证”。"));
    m_checkResult->setWordWrap(true);
    m_checkResult->setObjectName("dualCheckResult");
    layout->addWidget(m_checkResult);
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true); m_log->setMaximumHeight(65); m_log->setMinimumHeight(35);
    m_log->document()->setMaximumBlockCount(150);
    m_log->setObjectName("dualLog");
    layout->addWidget(m_log);
    connect(m_open, &QPushButton::clicked, this, &DualPanel::togglePorts);
    connect(m_refresh, &QPushButton::clicked, this, &DualPanel::refreshPorts);
    connect(clear, &QPushButton::clicked, this, [this]() {
        for (int i = 0; i < 2; ++i) {
            m_raw[i].clear(); m_text[i].clear(); m_decoders[i].reset();
            m_rxCount[i] = 0; m_dirty[i] = true;
        }
        render(); // Does not discard data already being verified or in the kernel.
    });
    connect(m_format, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this](int) {
        m_dirty[0] = m_dirty[1] = true; render();
    });
    m_busTimer.setSingleShot(true);
    connect(&m_busTimer, &QTimer::timeout, this, [this]() { m_busBusy = false; updateControls(); });
    m_checkTimer.setSingleShot(true);
    connect(&m_checkTimer, &QTimer::timeout, this, [this]() {
        finishCheck(false, tr("验证超时：对侧未收到完整测试字符串。检查端口、接线和是否有其他设备同时发送。"));
    });
    m_renderTimer.setInterval(50);
    connect(&m_renderTimer, &QTimer::timeout, this, &DualPanel::render);
    m_renderTimer.start();
    refreshPorts();
    m_portBoxes[0]->setEditText("/dev/ttymxc1");
    m_portBoxes[1]->setEditText("/dev/ttymxc2");
    m_dirty[0] = m_dirty[1] = true;
    updateControls(); render();
    log(tr("ELF1 原厂映射：A1/B1 = ttymxc1；A2/B2 = ttymxc2；ttymxc6 是 RS-232。RS-485 硬件自动选向。"));
    setStyleSheet("QGroupBox{border:1px solid #b9cdd5;border-radius:5px;margin-top:6px;}"
                  "QGroupBox::title{subcontrol-origin:margin;left:8px;}"
                  "QPlainTextEdit{background:#f6fafc;color:#183246;}"
                  "QPushButton{padding:4px 8px;}QComboBox{min-height:22px;}");
}

DualPanel::~DualPanel()
{
    for (auto *port : m_ports) port->disconnect(this);
    for (auto *port : m_ports) port->close();
}

bool DualPanel::isOpen() const { return m_ports[0]->isOpen() && m_ports[1]->isOpen(); }

void DualPanel::refreshPorts()
{
    for (int i = 0; i < 2; ++i) {
        const QString previous = m_portBoxes[i]->currentText();
        m_portBoxes[i]->clear();
        for (const auto &info : QSerialPortInfo::availablePorts()) {
            // Never offer ELF1's active system console in the dedicated RS-485 page.
            if (info.systemLocation() != "/dev/ttymxc0") m_portBoxes[i]->addItem(info.systemLocation());
        }
        m_portBoxes[i]->setEditText(previous);
    }
}

void DualPanel::togglePorts()
{
    if (isOpen()) { closePorts(); return; }
    QString names[2];
    for (int i = 0; i < 2; ++i) {
        names[i] = m_portBoxes[i]->currentText().trimmed();
        if (names[i].isEmpty()) { log(tr("请填写两个不同的串口。")); return; }
        const QString canonical = QFileInfo(names[i]).canonicalFilePath();
        if (!canonical.isEmpty()) names[i] = canonical;
        if (names[i] == "/dev/ttymxc0" || names[i] == "ttymxc0") {
            log(tr("ttymxc0 是 ELF1 系统控制台，本页禁止使用。")); return;
        }
    }
    if (names[0] == names[1]) { log(tr("双口互测必须选择两个不同的端口。")); return; }
    SerialOptions options;
    options.baud = m_baud->currentData().toInt();
    m_opening = true;
    bool ok = true;
    for (int i = 0; i < 2 && ok; ++i) { options.port = names[i]; ok = m_ports[i]->open(options); }
    m_opening = false;
    if (!ok) { closePorts(); return; } // Roll back the first port if the second fails.
    for (int i = 0; i < 2; ++i) m_decoders[i].reset();
    m_checkResult->setText(tr("两个端口已打开；尚未验证物理链路。请点击“验证 1 → 2”，再验证反向。"));
    log(tr("已同时打开 %1 和 %2，%3 / 8N1 / 无流控。启动和打开端口均不会自动发送。")
        .arg(names[0], names[1]).arg(options.baud));
    updateControls(); emit connectionChanged(true);
}

void DualPanel::closePorts()
{
    if (m_closing) return;
    m_closing = true;
    if (m_checkActive) finishCheck(false, tr("端口已关闭，验证中止。"));
    m_busTimer.stop(); m_busBusy = false;
    for (auto *port : m_ports) port->close();
    m_closing = false;
    updateControls(); emit connectionChanged(false);
}

void DualPanel::updateControls()
{
    const bool open = isOpen();
    const bool sending = m_busBusy || m_checkActive || m_ports[0]->pendingBytes() || m_ports[1]->pendingBytes();
    m_open->setText(open ? tr("关闭两个端口") : tr("打开两个端口"));
    m_status->setText(open ? tr("● 两个端口已打开") : tr("○ 未连接"));
    m_status->setStyleSheet(open ? "color:#13734f;font-weight:bold" : "color:#627582");
    m_baud->setEnabled(!open); m_refresh->setEnabled(!open);
    m_format->setEnabled(!sending);
    for (int i = 0; i < 2; ++i) {
        m_portBoxes[i]->setEnabled(!open);
        m_send[i]->setEnabled(open && !sending);
        m_verify[i]->setEnabled(open && !sending);
    }
}

void DualPanel::send(int side, bool verify)
{
    if (!isOpen() || m_busBusy || m_checkActive || m_ports[0]->pendingBytes() || m_ports[1]->pendingBytes()) return;
    QByteArray bytes;
    if (verify) {
        bytes = QString("[UART2UART %1->%2 %3]\n").arg(side + 1).arg(2 - side)
                .arg(QUuid::createUuid().toString()).toUtf8();
    } else if (m_format->currentIndex() == 1) {
        QString error;
        if (!ByteCodec::parseHex(m_tx[side]->toPlainText(), &bytes, &error)) { log(error); return; }
    } else bytes = m_tx[side]->toPlainText().toUtf8();
    if (bytes.isEmpty() || bytes.size() > 4096) { log(tr("双口页单次发送限 1～4096 字节。")); return; }
    // Conservative turnaround guard for this application's half-duplex transmissions.
    const int guardMs = int((qint64(bytes.size()) * 10000 + m_baud->currentData().toInt() - 1)
                           / m_baud->currentData().toInt()) + 30;
    m_busBusy = true; m_busTimer.start(guardMs);
    if (verify) {
        m_checkActive = true; m_checkTarget = 1 - side;
        m_expected = bytes; m_observed.clear(); m_checkTimer.start(3000 + guardMs);
        m_checkResult->setStyleSheet("color:#755400;");
        m_checkResult->setText(tr("正在验证 %1 → %2：等待对侧真实接收，最长约 3 秒……").arg(side + 1).arg(2 - side));
    }
    if (!m_ports[side]->send(bytes)) {
        m_busTimer.stop(); m_busBusy = false;
        if (m_checkActive) finishCheck(false, tr("发送失败。"));
    } else log(tr("接口%1 → 接口%2：加入发送队列 %3 字节%4。")
               .arg(side + 1).arg(2 - side).arg(bytes.size()).arg(verify ? tr("（链路验证）") : QString()));
    updateControls();
}

void DualPanel::receive(int side, const QByteArray &bytes)
{
    m_rxCount[side] += quint64(bytes.size());
    m_raw[side].append(bytes);
    if (m_raw[side].size() > 65536) m_raw[side] = m_raw[side].right(65536);
    m_text[side] += m_decoders[side].decode(bytes);
    if (m_text[side].size() > 32768) m_text[side] = m_text[side].right(32768);
    m_dirty[side] = true;
    if (m_checkActive && side == m_checkTarget) {
        m_observed.append(bytes);
        if (m_observed.contains(m_expected))
            finishCheck(true, tr("验证通过：接口%1 → 接口%2，对侧真实收到全部 %3 字节。")
                        .arg(2 - side).arg(side + 1).arg(m_expected.size()));
        else if (m_observed.size() > 8192) m_observed = m_observed.right(8192);
    }
}

void DualPanel::finishCheck(bool passed, const QString &message)
{
    m_checkActive = false; m_checkTimer.stop();
    m_checkResult->setText(message);
    m_checkResult->setStyleSheet(passed ? "color:#13734f;font-weight:bold" : "color:#a63232;font-weight:bold");
    log(message); updateControls(); emit verificationFinished(passed, message);
}

void DualPanel::render()
{
    for (int i = 0; i < 2; ++i) {
        if (!m_dirty[i]) continue;
        m_dirty[i] = false;
        m_rx[i]->setPlainText(m_format->currentIndex() == 1 ? ByteCodec::hex(m_raw[i].right(8192)) : m_text[i]);
        m_counts[i]->setText(tr("RX %1 B | TX %2 B | 待发 %3 B").arg(m_rxCount[i]).arg(m_txCount[i]).arg(m_ports[i]->pendingBytes()));
    }
}

void DualPanel::log(const QString &message)
{
    m_log->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss ") + message);
}
