#-------------------------------------------------
#
# Project created by QtCreator 2014-08-22T12:02:20
#
#-------------------------------------------------

QT       += core

QT       -= gui

TARGET = tiBackup
CONFIG   += console
CONFIG   -= app_bundle

TEMPLATE = app


SOURCES += main.cpp \
    lib/devicedisk.cpp \
    diskobserver.cpp

HEADERS += \
    lib/devicedisk.h \
    diskobserver.h

unix:!macx:!symbian: LIBS += -ludev


QMAKE_CXXFLAGS_DEBUG += -pipe
QMAKE_CXXFLAGS_RELEASE += -pipe -O2 -march=native
