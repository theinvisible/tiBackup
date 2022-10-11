#include "httpserve.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include "global.h"
#include "httplistener.h"
#include "httprequestmapper.h"

QString httpserve::searchConfigFile()
{
    QString binDir=QCoreApplication::applicationDirPath();
    QString fileName("tibackup_http.ini");

    QStringList searchList;
    searchList.append("/home/rene/DEV/qtcreator/tiBackup/var");
    searchList.append("/etc/tibackup");
    searchList.append(binDir);
    searchList.append(binDir+"/etc");
    searchList.append(binDir+"/../etc");
    searchList.append(QDir::rootPath()+"etc/opt");
    searchList.append(QDir::rootPath()+"etc");

    foreach (QString dir, searchList)
    {
        QFile file(dir+"/"+fileName);
        if (file.exists())
        {
            fileName=QDir(file.fileName()).canonicalPath();
            qDebug("Using config file %s",qPrintable(fileName));
            return fileName;
        }
    }

    // not found
    foreach (QString dir, searchList)
    {
        qWarning("%s/%s not found",qPrintable(dir),qPrintable(fileName));
    }
    qFatal("Cannot find config file %s",qPrintable(fileName));
    return nullptr;
}

httpserve::httpserve(QObject *parent)
    : QObject{parent}
{
    // Find the configuration file
    QString configFileName=searchConfigFile();

    // Configure logging into a file
    /*
    QSettings* logSettings=new QSettings(configFileName,QSettings::IniFormat,parent);
    logSettings->beginGroup("logging");
    FileLogger* logger=new FileLogger(logSettings,10000,parent);
    logger->installMsgHandler();
    */

    // Configure template loader and cache
    QSettings* templateSettings=new QSettings(configFileName,QSettings::IniFormat,parent);
    templateSettings->beginGroup("templates");
    templateCache=new TemplateCache(templateSettings,parent);

    // Configure session store
    QSettings* sessionSettings=new QSettings(configFileName,QSettings::IniFormat,parent);
    sessionSettings->beginGroup("sessions");
    sessionStore=new HttpSessionStore(sessionSettings,parent);

    // Configure static file controller
    QSettings* fileSettings=new QSettings(configFileName,QSettings::IniFormat,parent);
    fileSettings->beginGroup("docroot");
    staticFileController=new StaticFileController(fileSettings,parent);

    // Configure and start the TCP listener
    QSettings* listenerSettings=new QSettings(configFileName,QSettings::IniFormat,parent);
    listenerSettings->beginGroup("listener");

    new HttpListener(listenerSettings,new httprequestmapper(parent),parent);
}
