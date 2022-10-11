#include "../global.h"
#include "indexctrl.h"
#include "templatecache.h"
#include "template.h"

#include <ticonf.h>
#include "ipcclient.h"

#include <QDirIterator>

IndexCtrl::IndexCtrl()
{
}

void IndexCtrl::service(HttpRequest& request, HttpResponse& response)
{
    response.setHeader("Content-Type", "text/html; charset=UTF-8");

    tiConfBackupJobs jobss;
    jobss.readBackupJobs();

    Template t=templateCache->getTemplate("index",request.getHeader("Accept-Language"));
    t.enableWarnings();

    ipcClient *client = ipcClient::instance();
    QList<tiBackupJob*> jobs = jobss.getJobs();
    t.loop("jobs", jobs.size());
    for(int i=0; i < jobs.count(); i++)
    {
        tiBackupJob *job = jobs.at(i);

        backupManager::backupStatus status = client->getBackupStatus(job->name);

        t.setVariable(QString("jobs%1.name").arg(i), job->name);
        t.setVariable(QString("jobs%1.device").arg(i), job->device);
        t.setVariable(QString("jobs%1.partition_uuid").arg(i), job->partition_uuid);

        switch(status) {
        case backupManager::backupStatus::running:
            t.setVariable(QString("jobs%1.status").arg(i), tr("Running"));
            break;
        case backupManager::backupStatus::failed:
           t.setVariable(QString("jobs%1.status").arg(i), tr("Failed"));
            break;
        case backupManager::backupStatus::finished:
            t.setVariable(QString("jobs%1.status").arg(i), tr("Last run finished"));
            break;
        case backupManager::backupStatus::standby:
            t.setVariable(QString("jobs%1.status").arg(i), tr("Standby"));
            break;
        }
    }

    tiConfMain main_settings;
    QString backupdetaildir = main_settings.getLogsDetailDir();
    QDirIterator it_backupdetaildir(backupdetaildir);
    QString logfilepath;
    int j = 0;
    while (it_backupdetaildir.hasNext())
    {
        logfilepath = it_backupdetaildir.next();
        if(logfilepath.endsWith(".log")) {
            QFileInfo fi(logfilepath);
            QStringList b = fi.baseName().split("__");
            if(b.length() != 2)
                continue;

            j++;
        }
    }
    t.loop("backuplog", j);

    j = 0;
    QDirIterator it_backupdetaildir2(backupdetaildir);
    while (it_backupdetaildir2.hasNext())
    {
        logfilepath = it_backupdetaildir2.next();
        if(logfilepath.endsWith(".log"))
        {
            QFileInfo fi(logfilepath);
            QStringList b = fi.baseName().split("__");
            if(b.length() != 2)
                continue;

            t.setVariable(QString("backuplog%1.date").arg(j), b[0]);
            t.setVariable(QString("backuplog%1.name").arg(j), b[1]);
            t.setVariable(QString("backuplog%1.file").arg(j), fi.baseName());

            j++;
        }
    }

    response.write(t.toUtf8(),true);
}

void IndexCtrl::serviceBackupLog(HttpRequest &request, HttpResponse &response)
{
    response.setHeader("Content-Type", "text/html; charset=UTF-8");

    tiConfMain main_settings;
    QString backupdetaildir = main_settings.getLogsDetailDir();

    Template t=templateCache->getTemplate("backuplog", request.getHeader("Accept-Language"));
    t.enableWarnings();

    t.setVariable("name", request.getParameter("name"));
    QString logfile = QString("%1/%2.log").arg(backupdetaildir, QString(request.getParameter("name")));
    qDebug() << "logfile" << logfile;

    QFile file(logfile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream in(&file);
    t.setVariable("log", in.readAll());

    response.write(t.toUtf8(),true);
}
