QT += core gui widgets network serialport

CONFIG += c++11
TEMPLATE = app
TARGET = smartagriculture

SOURCES += \
    main.cpp \
    appconfig.cpp \
    mqttclient.cpp \
    sensorworker.cpp \
    sysfscontroller.cpp \
    canchannel.cpp \
    mainwindow.cpp

HEADERS += \
    appconfig.h \
    mqttclient.h \
    sensorworker.h \
    sysfscontroller.h \
    canchannel.h \
    mainwindow.h

unix:target.path = /usr/bin
INSTALLS += target
