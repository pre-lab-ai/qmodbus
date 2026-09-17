TEMPLATE = app
TARGET = controltransaction_test
CONFIG += console c++17
QT += core
INCLUDEPATH += ../src
SOURCES += controltransaction_test.cpp ../src/controltransaction.cpp ../src/controlvalidator.cpp ../src/pointmodel.cpp
HEADERS += ../src/controltransaction.h ../src/controlvalidator.h ../src/pointmodel.h
