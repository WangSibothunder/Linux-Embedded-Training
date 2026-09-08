#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>

class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString &sysfsOverride = QString(),
                        QWidget *parent = 0);

private slots:
    void discoverDevice();
    void refreshState();
    void applyPwm();
    void applyTimeout();

private:
    QString findDevicePath() const;
    bool isValidDevicePath(const QString &path) const;
    bool readUnsigned(const QString &name, unsigned int *value,
                      QString *error = 0) const;
    bool writeUnsigned(const QString &name, unsigned int value,
                       QString *error = 0);
    void setFanLevel(unsigned int level);
    void setControlsEnabled(bool enabled);
    void showResult(bool ok, const QString &message);

    QString m_sysfsOverride;
    QString m_devicePath;
    QLabel *m_deviceLabel;
    QLabel *m_stateLabel;
    QLabel *m_resultLabel;
    QPushButton *m_levelButtons[5];
    QSlider *m_pwmSlider;
    QLabel *m_pwmValueLabel;
    QSpinBox *m_timeoutSpin;
    QPushButton *m_applyPwmButton;
    QPushButton *m_applyTimeoutButton;
    QTimer *m_refreshTimer;
};

#endif
