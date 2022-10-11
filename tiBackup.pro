#-------------------------------------------------
#
# Project created by QtCreator 2014-08-22T12:02:20
#
#-------------------------------------------------

QT       += core network

QT       -= gui

TARGET = tiBackup
CONFIG   += console
CONFIG   -= app_bundle

TEMPLATE = app


SOURCES += main.cpp \
    diskmain.cpp \
    diskwatcher.cpp \
    global.cpp \
    httpctrl/indexctrl.cpp \
    httprequestmapper.cpp \
    httpserve.cpp

HEADERS += \
    diskmain.h \
    diskwatcher.h \
    global.h \
    httpctrl/indexctrl.h \
    httprequestmapper.h \
    httpserve.h

unix {
    message(Building for Unix)
    INCLUDEPATH += /usr/include/tibackuplib ../tiBackupLib
    LIBS += -L$$(HOME)/DEV/lib -ltiBackupLib
    QMAKE_CXXFLAGS_DEBUG += -pipe
    QMAKE_CXXFLAGS_RELEASE += -pipe -O2
}

OTHER_FILES += \
    init.d/tibackup

DISTFILES += \
    .gitlab-ci.yml \
    debian/changelog \
    debian/control \
    debian/rules \
    etc/tibackup_http.ini \
    systemd/tibackupd.service \
    var/tibackup_http.ini \
    var/www/docroot/index.html

#---------------------------------------------------------------------------------------
# The following lines include the sources of the QtWebAppLib library
#---------------------------------------------------------------------------------------

include(QtWebApp/logging/logging.pri)
include(QtWebApp/httpserver/httpserver.pri)
include(QtWebApp/templateengine/templateengine.pri)
