#ifndef DUALPANEL_H
#define DUALPANEL_H
#include <QWidget>
#include <QTimer>
#include "serialsession.h"
#include "bytecodec.h"
class QComboBox;
class QPushButton;
class QPlainTextEdit;
class QLabel;

// Two real, independent OS serial ports. RX is never simulated or echoed.
class DualPanel : public QWidget {
    Q_OBJECT
public:
    explicit DualPanel(QWidget *parent = nullptr);
    ~DualPanel();
    bool isOpen() const;
signals:
    void connectionChanged(bool open);
    void verificationFinished(bool passed, const QString &message);
private:
    void refreshPorts();
    void togglePorts();
    void closePorts();
    void updateControls();
    void render();
    void receive(int side, const QByteArray &bytes);
    void send(int side, bool verify);
    void finishCheck(bool passed, const QString &message);
    void log(const QString &message);
    SerialSession *m_ports[2];
    QComboBox *m_portBoxes[2], *m_baud, *m_format;
    QPushButton *m_open, *m_refresh, *m_send[2], *m_verify[2];
    QPlainTextEdit *m_tx[2], *m_rx[2], *m_log;
    QLabel *m_counts[2], *m_status, *m_checkResult;
    QByteArray m_raw[2];
    QString m_text[2];
    Utf8Stream m_decoders[2];
    quint64 m_rxCount[2] = {}, m_txCount[2] = {};
    bool m_dirty[2] = {}, m_closing = false, m_opening = false;
    QTimer m_renderTimer, m_busTimer, m_checkTimer;
    bool m_busBusy = false, m_checkActive = false;
    int m_checkTarget = -1;
    QByteArray m_expected, m_observed;
};
#endif
