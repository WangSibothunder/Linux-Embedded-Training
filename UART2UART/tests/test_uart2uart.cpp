#include <QtTest>
#include <QCheckBox>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include "bytecodec.h"
#include "serialsession.h"
#include "mainwindow.h"
#include "dualpanel.h"
#include "appwindow.h"
#include <QTabWidget>
#ifdef Q_OS_LINUX
#include <pty.h>
#include <unistd.h>
#include <fcntl.h>

// Only virtual ports: never enumerate/open a real hardware UART in tests.
class Pty {
public:
    Pty() {
        int slave = -1;
        char name[256] = {};
        if (::openpty(&master, &slave, name, nullptr, nullptr) == 0) {
            path = QString::fromLocal8Bit(name);
            ::close(slave);
            ::fcntl(master, F_SETFL, O_NONBLOCK);
        }
    }
    ~Pty() { disconnect(); }
    void disconnect() { if (master >= 0) ::close(master); master = -1; }
    QByteArray readAvailable() {
        char buffer[8192];
        QByteArray result;
        ssize_t n;
        while ((n = ::read(master, buffer, sizeof buffer)) > 0) result.append(buffer, int(n));
        return result;
    }
    int master = -1;
    QString path;
};
#endif

class UartTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QVERIFY(settingsDir.isValid());
        QCoreApplication::setOrganizationName("UART2UART-Tests");
        QCoreApplication::setApplicationName("isolated");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    }
    void init() { QSettings().clear(); }
    void hexRoundTrip() {
        QByteArray all, decoded;
        for (int i = 0; i < 256; ++i) all.append(char(i));
        QVERIFY(ByteCodec::parseHex(ByteCodec::hex(all), &decoded));
        QCOMPARE(decoded, all);
        QVERIFY(ByteCodec::parseHex("00 aB\n CD\tff", &decoded));
        QCOMPARE(decoded, QByteArray::fromHex("00abcdff"));
    }
    void invalidHex() {
        QByteArray decoded;
        QString error;
        for (const QString &input : {QString("0"), QString("0xFF"), QString("GG"), QString("01,02"), QString::fromUtf8("中文")}) {
            decoded = "old";
            QVERIFY(!ByteCodec::parseHex(input, &decoded, &error));
            QVERIFY(decoded.isEmpty());
            QVERIFY(!error.isEmpty());
        }
        QVERIFY(ByteCodec::parseHex(" \n", &decoded, &error));
        QVERIFY(decoded.isEmpty());
        QVERIFY(error.isEmpty());
    }
    void lineEndings() {
        QCOMPARE(ByteCodec::terminator(0), QByteArray());
        QCOMPARE(ByteCodec::terminator(1), QByteArray("\n"));
        QCOMPARE(ByteCodec::terminator(2), QByteArray("\r"));
        QCOMPARE(ByteCodec::terminator(3), QByteArray("\r\n"));
    }
    void splitUtf8() {
        const QString original = QString::fromUtf8("Hello 你好 串口 😀");
        const QByteArray bytes = original.toUtf8();
        for (int split = 0; split <= bytes.size(); ++split) {
            Utf8Stream decoder;
            QString result = decoder.decode(bytes.left(split));
            result += decoder.decode(bytes.mid(split));
            QCOMPARE(result, original);
        }
        Utf8Stream decoder;
        decoder.decode(QByteArray::fromHex("e4"));
        decoder.reset();
        QCOMPARE(decoder.decode("OK"), QString("OK"));
        QCOMPARE(decoder.decode(QByteArray("A\0B", 3)).size(), 3);
    }
    void unopenedAndInvalidPort() {
        SerialSession serial;
        QSignalSpy errors(&serial, SIGNAL(failure(QString)));
        QVERIFY(!serial.send("hello"));
        SerialOptions options;
        options.port = "/dev/uart2uart-does-not-exist";
        QVERIFY(!serial.open(options));
        QVERIFY(!serial.isOpen());
        QCOMPARE(serial.pendingBytes(), qint64(0));
        QVERIFY(errors.count() >= 2);
    }
    void bidirectionalBinary() {
#ifdef Q_OS_LINUX
        Pty pty;
        QVERIFY(pty.master >= 0);
        SerialSession serial;
        SerialOptions options; options.port = pty.path;
        QVERIFY(serial.open(options));
        QByteArray incoming;
        connect(&serial, &SerialSession::received, this, [&incoming](const QByteArray &b) { incoming += b; });
        QByteArray all;
        for (int i = 0; i < 256; ++i) all.append(char(i));
        QCOMPARE(::write(pty.master, all.constData(), size_t(all.size())), ssize_t(all.size()));
        QVERIFY(serial.send(all)); // TX and RX active together.
        QByteArray outgoing;
        QTRY_VERIFY((outgoing += pty.readAvailable()).size() == all.size());
        QTRY_COMPARE(incoming, all);
        QCOMPARE(outgoing, all);
        QTRY_COMPARE(serial.pendingBytes(), qint64(0));
        serial.close();
        QVERIFY(!serial.isOpen());
        QVERIFY(serial.open(options));
        QVERIFY(serial.send("again"));
        outgoing.clear();
        QTRY_VERIFY((outgoing += pty.readAvailable()) == QByteArray("again"));
#else
        QSKIP("PTY integration requires Linux");
#endif
    }
    void queueLimitsAndLargeWrite() {
#ifdef Q_OS_LINUX
        Pty pty;
        SerialSession serial;
        SerialOptions options; options.port = pty.path;
        QVERIFY(serial.open(options));
        QVERIFY(!serial.send(QByteArray()));
        QVERIFY(!serial.send(QByteArray(SerialSession::MaxPayload + 1, 'x')));
        QByteArray payload(SerialSession::MaxPayload, '\0');
        for (int i = 0; i < payload.size(); ++i) payload[i] = char(i % 256);
        qint64 written = 0;
        connect(&serial, &SerialSession::transmitted, this, [&written](qint64 n) { written += n; });
        for (int i = 0; i < 4; ++i) QVERIFY(serial.send(payload));
        QCOMPARE(serial.pendingBytes(), qint64(SerialSession::MaxQueue));
        QVERIFY(!serial.send("overflow"));
        QByteArray outgoing;
        QTRY_VERIFY((outgoing += pty.readAvailable()).size() == 4 * payload.size());
        QCOMPARE(outgoing, payload.repeated(4));
        QTRY_COMPARE(written, qint64(4 * payload.size()));
        QCOMPARE(serial.pendingBytes(), qint64(0));
#else
        QSKIP("PTY integration requires Linux");
#endif
    }
    void disconnectClosesSession() {
#ifdef Q_OS_LINUX
        Pty pty;
        SerialSession serial;
        SerialOptions options; options.port = pty.path;
        QSignalSpy errors(&serial, SIGNAL(failure(QString)));
        QVERIFY(serial.open(options));
        pty.disconnect();
        QTRY_VERIFY(!serial.isOpen());
        QVERIFY(!errors.isEmpty());
        QCOMPARE(serial.pendingBytes(), qint64(0));
#else
        QSKIP("PTY integration requires Linux");
#endif
    }
    void guiSendReceiveAndControls() {
#ifdef Q_OS_LINUX
        Pty pty;
        MainWindow window;
        window.resize(800, 480);
        window.show();
        auto *port = window.findChild<QComboBox *>("portBox");
        auto *open = window.findChild<QPushButton *>("openButton");
        auto *send = window.findChild<QPushButton *>("sendButton");
        auto *tx = window.findChild<QPlainTextEdit *>("sendEdit");
        auto *rx = window.findChild<QPlainTextEdit *>("receiveEdit");
        auto *repeat = window.findChild<QCheckBox *>("repeatBox");
        QVERIFY(port && open && send && tx && rx && repeat);
        QVERIFY(!send->isEnabled());
        port->setEditText(pty.path);
        open->click();
        QVERIFY(send->isEnabled());
        QVERIFY(!port->isEnabled());
        const QString greeting = QString::fromUtf8("Hello，另一台机器！\n");
        const QByteArray bytes = greeting.toUtf8();
        tx->setPlainText(greeting);
        send->click();
        QByteArray outgoing;
        QTRY_VERIFY((outgoing += pty.readAvailable()) == bytes);
        QCOMPARE(::write(pty.master, bytes.constData(), size_t(bytes.size())), ssize_t(bytes.size()));
        QTRY_COMPARE(rx->toPlainText(), greeting);
        QTest::qWait(100);
        QVERIFY(pty.readAvailable().isEmpty()); // No automatic echo loop.
        auto *mode = window.findChild<QComboBox *>("sendMode");
        mode->setCurrentIndex(1);
        tx->setPlainText("0xFF");
        send->click();
        QTest::qWait(50);
        QVERIFY(pty.readAvailable().isEmpty());
        tx->setPlainText("00 AB FF");
        window.findChild<QComboBox *>("lineEnding")->setCurrentIndex(3);
        send->click();
        outgoing.clear();
        QTRY_VERIFY((outgoing += pty.readAvailable()) == QByteArray::fromHex("00abff0d0a"));
        repeat->setChecked(true);
        QVERIFY(!send->isEnabled());
        QVERIFY(tx->isReadOnly());
        repeat->setChecked(false);
        QVERIFY(send->isEnabled());
        QVERIFY(!tx->isReadOnly());
        QTest::qWait(50);
        pty.readAvailable();
        // Record a real rendered GUI at a typical embedded LCD size.
        const QByteArray screenshotPath = qgetenv("UART2UART_SCREENSHOT");
        if (!screenshotPath.isEmpty()) QVERIFY(window.grab().save(QString::fromLocal8Bit(screenshotPath)));
        QVERIFY2(window.width() <= 800 && window.height() <= 480, "UI does not fit 800x480");
        window.findChild<QComboBox *>("receiveMode")->setCurrentIndex(1);
        QTRY_COMPARE(rx->toPlainText(), ByteCodec::hex(bytes));
        window.findChild<QPushButton *>("clearButton")->click();
        QVERIFY(rx->toPlainText().isEmpty());
        repeat->setChecked(true);
        open->click();
        QVERIFY(!repeat->isChecked());
        QVERIFY(!send->isEnabled());
        QVERIFY(port->isEnabled());
#else
        QSKIP("GUI/PTY integration requires Linux");
#endif
    }
    void dualRejectsSamePortAndRollsBack() {
#ifdef Q_OS_LINUX
        Pty a;
        DualPanel panel;
        auto *p0 = panel.findChild<QComboBox *>("dualPort0");
        auto *p1 = panel.findChild<QComboBox *>("dualPort1");
        auto *open = panel.findChild<QPushButton *>("dualOpen");
        p0->setEditText(a.path); p1->setEditText(a.path);
        open->click();
        QVERIFY(!panel.isOpen());
        p1->setEditText("/dev/uart2uart-nonexistent");
        open->click();
        QVERIFY(!panel.isOpen());
        SerialOptions options; options.port = a.path;
        SerialSession probe;
        QVERIFY(probe.open(options)); // First endpoint was released after second open failed.
#else
        QSKIP("PTY integration requires Linux");
#endif
    }
    void dualIndependentReceiveAndVerifiedTransfer() {
#ifdef Q_OS_LINUX
        Pty a, b;
        DualPanel panel;
        panel.resize(980, 540); panel.show();
        panel.findChild<QComboBox *>("dualPort0")->setEditText(a.path);
        panel.findChild<QComboBox *>("dualPort1")->setEditText(b.path);
        auto *open = panel.findChild<QPushButton *>("dualOpen");
        open->click();
        QVERIFY(panel.isOpen());
        QVERIFY(a.readAvailable().isEmpty()); QVERIFY(b.readAvailable().isEmpty());
        auto *rx0 = panel.findChild<QPlainTextEdit *>("dualRx0");
        auto *rx1 = panel.findChild<QPlainTextEdit *>("dualRx1");
        auto *send0 = panel.findChild<QPushButton *>("dualSend0");
        auto *send1 = panel.findChild<QPushButton *>("dualSend1");
        panel.findChild<QComboBox *>("dualFormat")->setCurrentIndex(1);
        panel.findChild<QPlainTextEdit *>("dualTx0")->setPlainText("00 01 7F 80 FF");
        send0->click();
        QVERIFY(!send1->isEnabled()); // Half-duplex turnaround guard.
        QByteArray outgoing;
        QTRY_VERIFY((outgoing += a.readAvailable()).size() == 5);
        QCOMPARE(outgoing, QByteArray::fromHex("00017f80ff"));
        QCOMPARE(::write(b.master, outgoing.constData(), size_t(outgoing.size())), ssize_t(outgoing.size()));
        QTRY_COMPARE(rx1->toPlainText(), QString("00 01 7F 80 FF"));
        QVERIFY(rx0->toPlainText().isEmpty()); // No fabricated local receive.
        QTRY_VERIFY(send1->isEnabled());
        QSignalSpy verified(&panel, SIGNAL(verificationFinished(bool,QString)));
        auto relay = [&]() {
            const QByteArray fromA = a.readAvailable(), fromB = b.readAvailable();
            if (!fromA.isEmpty()) ::write(b.master, fromA.constData(), size_t(fromA.size()));
            if (!fromB.isEmpty()) ::write(a.master, fromB.constData(), size_t(fromB.size()));
            return !verified.isEmpty();
        };
        panel.findChild<QPushButton *>("dualVerify0")->click();
        QTRY_VERIFY(relay());
        QVERIFY(verified.takeFirst().at(0).toBool());
        QTRY_VERIFY(send1->isEnabled());
        panel.findChild<QPushButton *>("dualVerify1")->click();
        QTRY_VERIFY(relay());
        QVERIFY(verified.takeFirst().at(0).toBool());
        panel.findChild<QComboBox *>("dualFormat")->setCurrentIndex(0);
        const QByteArray screenshotPath = qgetenv("UART2UART_DUAL_SCREENSHOT");
        if (!screenshotPath.isEmpty()) QVERIFY(panel.grab().save(QString::fromLocal8Bit(screenshotPath)));
        a.disconnect();
        QTRY_VERIFY(!panel.isOpen());
        QVERIFY(!send0->isEnabled()); QVERIFY(!send1->isEnabled());
#else
        QSKIP("PTY integration requires Linux");
#endif
    }
    void dualDoesNotPassWithoutPhysicalRelay() {
#ifdef Q_OS_LINUX
        Pty a, b;
        DualPanel panel;
        panel.findChild<QComboBox *>("dualPort0")->setEditText(a.path);
        panel.findChild<QComboBox *>("dualPort1")->setEditText(b.path);
        panel.findChild<QPushButton *>("dualOpen")->click();
        QVERIFY(panel.isOpen());
        QSignalSpy verified(&panel, SIGNAL(verificationFinished(bool,QString)));
        panel.findChild<QPushButton *>("dualVerify0")->click();
        QByteArray outgoing;
        QTRY_VERIFY(!(outgoing += a.readAvailable()).isEmpty());
        // Feed own TX back into sender. Only the other endpoint can satisfy verification.
        QCOMPARE(::write(a.master, outgoing.constData(), size_t(outgoing.size())), ssize_t(outgoing.size()));
        QTRY_COMPARE(verified.count(), 1);
        QVERIFY(!verified.at(0).at(0).toBool());
        QVERIFY(panel.findChild<QPlainTextEdit *>("dualRx1")->toPlainText().isEmpty());
#else
        QSKIP("PTY integration requires Linux");
#endif
    }
    void modesPreventConcurrentPortOwnership() {
#ifdef Q_OS_LINUX
        Pty a, b;
        AppWindow window;
        auto *tabs = window.findChild<QTabWidget *>("appTabs");
        window.findChild<QComboBox *>("dualPort0")->setEditText(a.path);
        window.findChild<QComboBox *>("dualPort1")->setEditText(b.path);
        window.findChild<QPushButton *>("dualOpen")->click();
        QVERIFY(!tabs->isTabEnabled(1));
        window.findChild<QPushButton *>("dualOpen")->click();
        QVERIFY(tabs->isTabEnabled(1));
        tabs->setCurrentIndex(1);
        window.findChild<QComboBox *>("portBox")->setEditText(a.path);
        window.findChild<QPushButton *>("openButton")->click();
        QVERIFY(!tabs->isTabEnabled(0));
        window.findChild<QPushButton *>("openButton")->click();
        QVERIFY(tabs->isTabEnabled(0));
#else
        QSKIP("PTY integration requires Linux");
#endif
    }
private:
    QTemporaryDir settingsDir;
};

QTEST_MAIN(UartTests)
#include "test_uart2uart.moc"
