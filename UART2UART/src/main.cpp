#include "appwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName("UART2UART");
    app.setApplicationName("UART2UART");
    app.setApplicationVersion("2.0.0");
    AppWindow window(app.arguments().contains("--single"));
    if (app.arguments().contains("--fullscreen")) window.showFullScreen();
    else window.show();
    return app.exec();
}
