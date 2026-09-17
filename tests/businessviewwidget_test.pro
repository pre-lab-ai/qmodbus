TEMPLATE = app
TARGET = businessviewwidget_test
CONFIG += console c++17
QT += core gui widgets sql

INCLUDEPATH += ../src ../3rdparty/libmodbus/src

SOURCES += businessviewwidget_test.cpp \
           ../src/businessviewwidget.cpp \
           ../src/acquisitionstore.cpp \
           ../src/pollscheduler.cpp \
           ../src/pointmodel.cpp

HEADERS += ../src/businessviewwidget.h \
           ../src/acquisitionstore.h \
           ../src/qualitycode.h \
           ../src/pollscheduler.h \
           ../src/pointmodel.h

RESOURCES += ../data/qmodbus.qrc
