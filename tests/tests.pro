QT += core gui network testlib widgets xml

CONFIG += console testcase c++11
CONFIG -= app_bundle

TEMPLATE = app
TARGET = lmc-regression-tests

INCLUDEPATH += ../lmc/src

SOURCES += \
    tst_regressions.cpp \
    trace_stub.cpp \
    ../lmc/src/netstreamer.cpp \
    ../lmc/src/filemodelview.cpp

HEADERS += \
    ../lmc/src/netstreamer.h \
    ../lmc/src/filemodelview.h
