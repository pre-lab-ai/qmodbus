TARGET = qmodbus
TEMPLATE = app
VERSION = 0.1.0

QT += gui widgets serialport sql

SOURCES += src/main.cpp \
    src/mainwindow.cpp \
    src/modbussession.cpp \
    src/pointmodel.cpp \
    src/pollscheduler.cpp \
    src/acquisitionstore.cpp \
    src/businessviewwidget.cpp \
    src/controlvalidator.cpp \
    src/controltransaction.cpp \
    src/alarmstate.cpp \
    src/BatchProcessor.cpp \
    3rdparty/libmodbus/src/modbus.c \
    3rdparty/libmodbus/src/modbus-data.c \
    3rdparty/libmodbus/src/modbus-rtu.c \
    3rdparty/libmodbus/src/modbus-tcp.c \
    3rdparty/libmodbus/src/modbus-ascii.c \
    src/asciisettingswidget.cpp \
    src/rtusettingswidget.cpp \
    src/serialsettingswidget.cpp \
    src/tcpipsettingswidget.cpp \
    src/ipaddressctrl.cpp \
    src/iplineedit.cpp

HEADERS += src/mainwindow.h \
    src/modbussession.h \
    src/pointmodel.h \
    src/pollscheduler.h \
    src/acquisitionstore.h \
    src/businessviewwidget.h \
    src/controlvalidator.h \
    src/controltransaction.h \
    src/alarmstate.h \
    src/qualitycode.h \
    src/BatchProcessor.h \
    3rdparty/libmodbus/src/modbus.h \
    src/serialsettingswidget.h \
    src/imodbus.h \
    src/tcpipsettingswidget.h \
    src/ipaddressctrl.h \
    src/iplineedit.h

INCLUDEPATH += 3rdparty/libmodbus \
               3rdparty/libmodbus/src \
               src
unix {
    DEFINES += _TTY_POSIX_
}

win32 {
    DEFINES += _TTY_WIN_  WINVER=0x0501  WIN32_LEAN_AND_MEAN
    LIBS += -lws2_32 -luser32 -ladvapi32
}

FORMS += forms/mainwindow.ui \
    forms/about.ui	\
    forms/BatchProcessor.ui \
    forms/serialsettingswidget.ui \
    forms/tcpipsettingswidget.ui \
    forms/ipaddressctrl.ui

RESOURCES += data/qmodbus.qrc

RC_FILE += qmodbus.rc

include(deployment.pri)
