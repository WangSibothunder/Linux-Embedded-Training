#ifndef APPWINDOW_H
#define APPWINDOW_H
#include <QMainWindow>
class AppWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit AppWindow(bool singlePort = false, QWidget *parent = nullptr);
};
#endif
