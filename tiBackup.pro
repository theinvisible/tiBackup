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
    diskmain.cpp \
    diskwatcher.cpp

HEADERS += \
    diskmain.h \
    diskwatcher.h

unix {
    message(Building for Unix)
    INCLUDEPATH += /home/rene/DEV/qtcreator/tiBackupLib
    LIBS += -L/home/rene/DEV/lib -ltiBackupLib
    QMAKE_CXXFLAGS_DEBUG += -pipe
    QMAKE_CXXFLAGS_RELEASE += -pipe -O2
}

OTHER_FILES += \
    init.d/tibackup
