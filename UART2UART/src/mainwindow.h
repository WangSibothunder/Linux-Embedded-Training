#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QMainWindow>
#include <QTimer>
#include "serialsession.h"
#include "bytecodec.h"
class QComboBox;
class QPushButton;
class QPlainTextEdit;
class QCheckBox;
class QSpinBox;
class QLabel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
signals:
    void connectionChanged(bool open);
private:
    void buildUi();
    void refreshPorts();
    void togglePort();
    bool payload(QByteArray *result);
    void sendOnce();
    void startRepeat(bool on);
    void updateControls();
    void onReceive(const QByteArray &data);
    void renderReceive();
    void clearReceive();
    void saveReceive();
    void log(const QString &text, bool error = false);
    void updateCounts();
    void loadSettings();
    void saveSettings();

    SerialSession m_serial;
    QComboBox *m_portBox, *m_baudBox, *m_dataBox, *m_parityBox, *m_stopBox, *m_flowBox;
    QComboBox *m_sendMode, *m_lineEnding, *m_receiveMode;
    QPushButton *m_refresh, *m_open, *m_send;
    QPlainTextEdit *m_receiveEdit, *m_sendEdit, *m_logEdit;
    QCheckBox *m_repeat, *m_autoScroll;
    QSpinBox *m_interval;
    QLabel *m_state, *m_counts;
    QTimer m_repeatTimer, m_renderTimer;
    QByteArray m_rxBuffer, m_repeatPayload;
    QString m_textTail;
    Utf8Stream m_decoder;
    quint64 m_rxCount = 0, m_txCount = 0, m_dropped = 0;
    bool m_dirty = false;
};
#endif
