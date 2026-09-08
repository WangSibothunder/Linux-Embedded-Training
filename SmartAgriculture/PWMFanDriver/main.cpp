#include "mainwindow.h"

#include <QApplication>
#include <QString>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("PWMFanControl");
    QApplication::setApplicationVersion("1.0.0");

    QString sysfsOverride;
    for (int i = 1; i + 1 < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--sysfs"))
            sysfsOverride = QString::fromLocal8Bit(argv[++i]);
    }

    MainWindow window(sysfsOverride);
    window.show();
    return app.exec();
}
