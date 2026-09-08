#include "mainwindow.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace {
const unsigned int kLevelPwm[5] = { 0, 64, 128, 192, 255 };
}

MainWindow::MainWindow(const QString &sysfsOverride, QWidget *parent)
    : QMainWindow(parent),
      m_sysfsOverride(sysfsOverride),
      m_deviceLabel(new QLabel),
      m_stateLabel(new QLabel(QStringLiteral("正在查找风扇驱动……"))),
      m_resultLabel(new QLabel),
      m_pwmSlider(new QSlider(Qt::Horizontal)),
      m_pwmValueLabel(new QLabel(QStringLiteral("0"))),
      m_timeoutSpin(new QSpinBox),
      m_applyPwmButton(new QPushButton(QStringLiteral("应用 PWM"))),
      m_applyTimeoutButton(new QPushButton(QStringLiteral("应用超时"))),
      m_refreshTimer(new QTimer(this))
{
    setWindowTitle(QStringLiteral("PWM7 风扇控制"));
    resize(720, 420);

    QWidget *central = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(central);
    QFont titleFont;
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    QLabel *title = new QLabel(QStringLiteral("PWM7 风扇控制面板"));
    title->setFont(titleFont);
    layout->addWidget(title);

    m_deviceLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_deviceLabel->setWordWrap(true);
    layout->addWidget(m_deviceLabel);
    layout->addWidget(m_stateLabel);

    QGridLayout *levels = new QGridLayout;
    const QStringList names = QStringList()
            << QStringLiteral("关闭") << QStringLiteral("1 档")
            << QStringLiteral("2 档") << QStringLiteral("3 档")
            << QStringLiteral("4 档");
    for (int i = 0; i < 5; ++i) {
        m_levelButtons[i] = new QPushButton(names.at(i));
        m_levelButtons[i]->setMinimumHeight(54);
        levels->addWidget(m_levelButtons[i], 0, i);
        connect(m_levelButtons[i], &QPushButton::clicked,
                this, [this, i]() { setFanLevel(static_cast<unsigned int>(i)); });
    }
    layout->addLayout(levels);

    QHBoxLayout *pwmRow = new QHBoxLayout;
    pwmRow->addWidget(new QLabel(QStringLiteral("精细 PWM（0～255）：")));
    m_pwmSlider->setRange(0, 255);
    m_pwmSlider->setTickInterval(16);
    m_pwmSlider->setTracking(false);
    pwmRow->addWidget(m_pwmSlider, 1);
    m_pwmValueLabel->setMinimumWidth(40);
    pwmRow->addWidget(m_pwmValueLabel);
    pwmRow->addWidget(m_applyPwmButton);
    layout->addLayout(pwmRow);

    QHBoxLayout *timeoutRow = new QHBoxLayout;
    timeoutRow->addWidget(new QLabel(QStringLiteral("自动关闭（秒，0 表示禁用）：")));
    m_timeoutSpin->setRange(0, 86400);
    m_timeoutSpin->setSuffix(QStringLiteral(" 秒"));
    timeoutRow->addWidget(m_timeoutSpin);
    timeoutRow->addWidget(m_applyTimeoutButton);
    timeoutRow->addStretch(1);
    layout->addLayout(timeoutRow);

    m_resultLabel->setWordWrap(true);
    layout->addWidget(m_resultLabel);
    layout->addStretch(1);
    setCentralWidget(central);

    connect(m_pwmSlider, &QSlider::valueChanged, this, [this](int value) {
        m_pwmValueLabel->setText(QString::number(value));
    });
    connect(m_applyPwmButton, &QPushButton::clicked,
            this, &MainWindow::applyPwm);
    connect(m_applyTimeoutButton, &QPushButton::clicked,
            this, &MainWindow::applyTimeout);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::refreshState);
    m_refreshTimer->start(1000);
    discoverDevice();
}

bool MainWindow::isValidDevicePath(const QString &path) const
{
    const QDir dir(path);
    return dir.exists(QStringLiteral("pwm")) &&
           dir.exists(QStringLiteral("fan_level")) &&
           dir.exists(QStringLiteral("fan_timeout"));
}

QString MainWindow::findDevicePath() const
{
    if (!m_sysfsOverride.isEmpty())
        return isValidDevicePath(m_sysfsOverride) ? m_sysfsOverride : QString();

    const QString direct = QStringLiteral("/sys/bus/platform/devices/pwm-fan-self");
    if (isValidDevicePath(direct))
        return direct;

    QDirIterator it(QStringLiteral("/sys/bus/platform/devices"),
                    QDir::Dirs | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        const QString candidate = it.next();
        if (isValidDevicePath(candidate))
            return candidate;
    }
    return QString();
}

void MainWindow::discoverDevice()
{
    m_devicePath = findDevicePath();
    const bool found = !m_devicePath.isEmpty();
    m_deviceLabel->setText(found
        ? QStringLiteral("驱动路径：%1").arg(m_devicePath)
        : QStringLiteral("未找到 pwm-fan-self 驱动的 sysfs 属性"));
    setControlsEnabled(found);
    if (found)
        refreshState();
    else
        m_stateLabel->setText(QStringLiteral("请先加载 pwm-fan-self.ko，并确认新设备树已经生效。"));
}

bool MainWindow::readUnsigned(const QString &name, unsigned int *value,
                              QString *error) const
{
    QFile file(QDir(m_devicePath).filePath(name));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    bool ok = false;
    const uint result = QString::fromLatin1(file.readAll()).trimmed().toUInt(&ok);
    if (!ok) {
        if (error) *error = QStringLiteral("驱动返回了无效数值");
        return false;
    }
    *value = result;
    return true;
}

bool MainWindow::writeUnsigned(const QString &name, unsigned int value,
                               QString *error)
{
    QFile file(QDir(m_devicePath).filePath(name));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    const QByteArray data = QByteArray::number(value) + '\n';
    if (file.write(data) != data.size()) {
        if (error) *error = file.errorString();
        return false;
    }
    if (!file.flush()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

void MainWindow::refreshState()
{
    if (m_devicePath.isEmpty() || !isValidDevicePath(m_devicePath)) {
        discoverDevice();
        return;
    }
    unsigned int pwm = 0, level = 0, timeout = 0;
    QString error;
    if (!readUnsigned(QStringLiteral("pwm"), &pwm, &error) ||
        !readUnsigned(QStringLiteral("fan_level"), &level, &error) ||
        !readUnsigned(QStringLiteral("fan_timeout"), &timeout, &error)) {
        showResult(false, QStringLiteral("读取状态失败：%1").arg(error));
        return;
    }
    if (!m_pwmSlider->isSliderDown())
        m_pwmSlider->setValue(static_cast<int>(pwm));
    if (!m_timeoutSpin->hasFocus())
        m_timeoutSpin->setValue(static_cast<int>(timeout));
    m_stateLabel->setText(QStringLiteral("当前状态：%1 档，PWM=%2，自动关闭=%3 秒")
                          .arg(level).arg(pwm).arg(timeout));
}

void MainWindow::setFanLevel(unsigned int level)
{
    QString error;
    const bool ok = writeUnsigned(QStringLiteral("fan_level"), level, &error);
    showResult(ok, ok ? QStringLiteral("已切换到 %1 档（PWM=%2）")
                       .arg(level).arg(kLevelPwm[level])
                     : QStringLiteral("设置档位失败：%1").arg(error));
    if (ok) refreshState();
}

void MainWindow::applyPwm()
{
    QString error;
    const unsigned int value = static_cast<unsigned int>(m_pwmSlider->value());
    const bool ok = writeUnsigned(QStringLiteral("pwm"), value, &error);
    showResult(ok, ok ? QStringLiteral("PWM 已设置为 %1").arg(value)
                     : QStringLiteral("设置 PWM 失败：%1").arg(error));
    if (ok) refreshState();
}

void MainWindow::applyTimeout()
{
    QString error;
    const unsigned int value = static_cast<unsigned int>(m_timeoutSpin->value());
    const bool ok = writeUnsigned(QStringLiteral("fan_timeout"), value, &error);
    showResult(ok, ok ? QStringLiteral("自动关闭已设置为 %1 秒").arg(value)
                     : QStringLiteral("设置超时失败：%1").arg(error));
    if (ok) refreshState();
}

void MainWindow::setControlsEnabled(bool enabled)
{
    for (int i = 0; i < 5; ++i)
        m_levelButtons[i]->setEnabled(enabled);
    m_pwmSlider->setEnabled(enabled);
    m_timeoutSpin->setEnabled(enabled);
    m_applyPwmButton->setEnabled(enabled);
    m_applyTimeoutButton->setEnabled(enabled);
}

void MainWindow::showResult(bool ok, const QString &message)
{
    m_resultLabel->setStyleSheet(ok ? QStringLiteral("color:#18794e")
                                    : QStringLiteral("color:#b42318"));
    m_resultLabel->setText(message);
}
