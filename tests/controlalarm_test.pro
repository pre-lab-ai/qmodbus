TEMPLATE = app
TARGET = controlalarm_test
CONFIG += console c++17
QT += core
INCLUDEPATH += ../src
SOURCES += controlalarm_test.cpp ../src/controlvalidator.cpp ../src/alarmstate.cpp ../src/pointmodel.cpp
HEADERS += ../src/controlvalidator.h ../src/alarmstate.h ../src/pointmodel.h
