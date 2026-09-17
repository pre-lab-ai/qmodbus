TEMPLATE = app
TARGET = acquisitionstore_test
QT += core sql
CONFIG += console c++17
CONFIG -= app_bundle

SOURCES += acquisitionstore_test.cpp \
           ../src/acquisitionstore.cpp \
           ../src/pollscheduler.cpp \
           ../src/pointmodel.cpp

HEADERS += ../src/acquisitionstore.h \
           ../src/qualitycode.h \
           ../src/pollscheduler.h \
           ../src/pointmodel.h

INCLUDEPATH += ../src \
               ../3rdparty/libmodbus/src
