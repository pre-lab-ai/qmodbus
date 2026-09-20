QT += core network
CONFIG += console c++17
TEMPLATE = app
TARGET = modbusresponder_test
INCLUDEPATH += ../src ../3rdparty/libmodbus ../3rdparty/libmodbus/src
SOURCES += modbusresponder_test.cpp \
           ../src/modbusresponder.cpp \
           ../3rdparty/libmodbus/src/modbus.c \
           ../3rdparty/libmodbus/src/modbus-data.c \
           ../3rdparty/libmodbus/src/modbus-rtu.c \
           ../3rdparty/libmodbus/src/modbus-tcp.c \
           ../3rdparty/libmodbus/src/modbus-ascii.c
HEADERS += ../src/modbusresponder.h \
           ../3rdparty/libmodbus/src/modbus.h
win32:LIBS += -lws2_32 -luser32 -ladvapi32
