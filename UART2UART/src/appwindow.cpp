#include "appwindow.h"
#include "dualpanel.h"
#include "mainwindow.h"
#include <QTabWidget>

AppWindow::AppWindow(bool singlePort, QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("UART2UART 2.0 · RS-485 双口对测"));
    resize(1000, 640);
    auto *tabs = new QTabWidget(this);
    tabs->setObjectName("appTabs");
    auto *dual = new DualPanel(tabs);
    auto *single = new MainWindow(tabs);
    single->setWindowFlags(Qt::Widget);
    tabs->addTab(dual, tr("RS-485 双口对测 · A1/B1 ↔ A2/B2"));
    tabs->addTab(single, tr("单串口工具 · 原有功能"));
    tabs->setCurrentIndex(singlePort ? 1 : 0);
    setCentralWidget(tabs);
    // Switching modes requires closing the active mode's ports first.
    connect(dual, &DualPanel::connectionChanged, this, [tabs](bool open) { tabs->setTabEnabled(1, !open); });
    connect(single, &MainWindow::connectionChanged, this, [tabs](bool open) { tabs->setTabEnabled(0, !open); });
}
