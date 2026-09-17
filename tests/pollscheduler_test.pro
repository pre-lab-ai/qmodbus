TEMPLATE = app
TARGET = pollscheduler_test
QT += core
CONFIG += console c++11
CONFIG -= app_bundle

SOURCES += pollscheduler_test.cpp \
           ../src/pollscheduler.cpp \
           ../src/pointmodel.cpp

HEADERS += ../src/pollscheduler.h \
           ../src/pointmodel.h

INCLUDEPATH += ../src \
               ../3rdparty/libmodbus/src
