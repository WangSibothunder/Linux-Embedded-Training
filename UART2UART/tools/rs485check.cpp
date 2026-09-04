#include "serialsession.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QTextStream>
#include <QTimer>
#include <QUuid>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    if (argc != 3) {
        out << "Usage: rs485check /dev/PORT1 /dev/PORT2\n"
               "WARNING: sends 10 alternating test frames at 115200 8N1, no flow control.\n";
        return 2;
    }
    const QString a = QFileInfo(QString::fromLocal8Bit(argv[1])).canonicalFilePath();
    const QString b = QFileInfo(QString::fromLocal8Bit(argv[2])).canonicalFilePath();
    if (a.isEmpty() || b.isEmpty() || a == b || a == "/dev/ttymxc0" || b == "/dev/ttymxc0") {
        out << "FAIL: require two existing, distinct, non-console serial ports.\n";
        return 2;
    }
    SerialSession ports[2];
    for (auto &p : ports) QObject::connect(&p, &SerialSession::failure, &app, [&](const QString &s) {
        out << "SERIAL_ERROR: " << s << endl;
        app.exit(1);
    });
    SerialOptions options;
    options.port = a;
    if (!ports[0].open(options)) return 1;
    options.port = b;
    if (!ports[1].open(options)) return 1;
    int round = 0;
    bool waiting = false;
    QByteArray expected, collected;
    QTimer timeout, turn;
    timeout.setSingleShot(true); turn.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &app, [&]() {
        out << "FAIL round=" << round + 1 << " direction=" << (round % 2) + 1
            << "->" << 2 - (round % 2) << " received=" << collected.toHex() << endl;
        app.exit(1);
    });
    QObject::connect(&turn, &QTimer::timeout, &app, [&]() {
        const int sender = round % 2;
        expected = QString("UART2UART-RS485 round=%1 %2\n").arg(round + 1)
                .arg(QUuid::createUuid().toString()).toUtf8();
        if (round >= 2) for (int i = 0; i < 256; ++i) expected.append(char(i));
        collected.clear(); waiting = true; timeout.start(3000);
        if (!ports[sender].send(expected)) app.exit(1);
    });
    for (int side = 0; side < 2; ++side) {
        QObject::connect(&ports[side], &SerialSession::received, &app, [&, side](const QByteArray &data) {
            if (!waiting || side != 1 - (round % 2)) return;
            collected += data;
            if (!expected.startsWith(collected)) {
                out << "FAIL: byte mismatch round=" << round + 1 << endl;
                app.exit(1); return;
            }
            if (collected != expected) return;
            timeout.stop(); waiting = false;
            out << "PASS round=" << round + 1 << " direction=" << (round % 2) + 1
                << "->" << side + 1 << " bytes=" << expected.size() << endl;
            if (++round == 10) { out << "ALL_10_TRANSFERS_PASSED" << endl; app.exit(0); }
            else turn.start(100); // Half-duplex guard; never simultaneous transmit.
        });
    }
    turn.start(100);
    return app.exec();
}
