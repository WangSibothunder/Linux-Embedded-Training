#include "appconfig.h"
#include "mainwindow.h"

#include <QApplication>
#include <QFileInfo>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("SmartAgricultureTerminal"));

    QString configPath = QStringLiteral("/etc/smart-agriculture/config.ini");
    for (int i = 1; i + 1 < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--config"))
            configPath = QString::fromLocal8Bit(argv[++i]);
    }
    if (!QFileInfo(configPath).exists() && QFileInfo(QStringLiteral("config.ini")).exists())
        configPath = QStringLiteral("config.ini");

    QString error;
    const AppConfig config = AppConfig::load(configPath, &error);
    if (!config.isValid(&error)) {
        QMessageBox::critical(0, QStringLiteral("配置错误"), error);
        return 2;
    }

    MainWindow window(config);
    window.show();
    return application.exec();
}
