QT += core serialport
QT -= gui
CONFIG += c++11 console warn_on
TEMPLATE = app
TARGET = rs485check
INCLUDEPATH += ../src
SOURCES += rs485check.cpp ../src/serialsession.cpp
HEADERS += ../src/serialsession.h
