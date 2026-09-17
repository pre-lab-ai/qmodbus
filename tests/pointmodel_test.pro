TEMPLATE = app
TARGET = pointmodel_test
QT += core
CONFIG += console c++11
CONFIG -= app_bundle

SOURCES += pointmodel_test.cpp \
           ../src/pointmodel.cpp

HEADERS += ../src/pointmodel.h

INCLUDEPATH += ../src \
               ../3rdparty/libmodbus/src
