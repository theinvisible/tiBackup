/*
 *
tiBackup - A intelligent desktop/standalone backup system daemon

Copyright (C) 2014 Rene Hadler, rene@hadler.me, https://hadler.me

    This file is part of tiBackup.

    tiBackup is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    tiBackup is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with tiBackup.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "diskmain.h"

#include "iostream"

#include <QDebug>
#include <QThread>
#include <QDateTime>
#include <QTimer>

#include "config.h"
#include "tibackupdiskobserver.h"
#include "diskwatcher.h"
#include "ticonf.h"
#include "workers/tibackupjobworker.h"

DiskMain::DiskMain(QObject *parent) : QObject(parent)
{
    std::cout << "Starting tiBackup Server " << QString(tibackup_config::version).toStdString() << std::endl;

    manager = new backupManager(this);

    QThread* thread = new QThread;
    DiskWatcher* worker = new DiskWatcher();
    worker->moveToThread(thread);
    //connect(worker, SIGNAL(error(QString)), this, SLOT(errorString(QString)));
    connect(thread, SIGNAL(started()), worker, SLOT(process()));
    connect(worker, SIGNAL(finished()), thread, SLOT(quit()));
    connect(worker, SIGNAL(finished()), worker, SLOT(deleteLater()));
    connect(thread, SIGNAL(finished()), thread, SLOT(deleteLater()));
    connect(worker, SIGNAL(diskRemoved(DeviceDisk*)), this, SLOT(onDiskRemoved(DeviceDisk*)));
    connect(worker, SIGNAL(diskAdded(DeviceDisk*)), this, SLOT(onDiskAdded(DeviceDisk*)));
    thread->start();

    // We start a timer for task observation
    QTimer *timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(onTaskCheck()));
    timer->start(1000*60);

    onTaskCheck();
}

void DiskMain::onDiskRemoved(DeviceDisk *disk)
{
    qInfo() << "Disk removed:" << disk->name;
}

void DiskMain::onDiskAdded(DeviceDisk *disk)
{
    qInfo() << "Disk added:" << disk->name;

    disk->readPartitions();
    qDebug() << "DiskMain::onDiskAdded() -> diskpartcount::" << disk->partitions.count();
    for(int i=0; i < disk->partitions.count(); i++)
    {
        DeviceDiskPartition part = disk->partitions.at(i);
        qDebug() << "DiskMain::onDiskAdded() -> disk partition added::" << part.uuid;

        tiConfBackupJobs objjobs;
        QList<tiBackupJob*> jobs = objjobs.getJobsByUuid(part.uuid);
        for(int j=0; j < jobs.count(); j++)
        {
            tiBackupJob *job = jobs.at(j);
            qDebug() << "DiskMain::onDiskAdded() -> job found for uuid::" << job->name;

            if(job->start_backup_on_hotplug == false)
                continue;

            qInfo() << "Starting backup on hotplug:" << job->name;
            manager->startBackup(job->name);
        }
    }
}

void DiskMain::onTaskCheck()
{
    QDateTime curDate = QDateTime::currentDateTime();

    qDebug() << "DiskMain::onTaskCheck()-> " << curDate.toString("MMM d hh:mm:ss");

    // We check now all jobs if they have a task that meet the current condition
    tiConfBackupJobs objjobs;
    objjobs.readBackupJobs();
    QList<tiBackupJob*> jobs = objjobs.getJobs();
    for(int j=0; j < jobs.count(); j++)
    {
        tiBackupJob *job = jobs.at(j);

        if(job->intervalType == tiBackupJobInterval::NONE)
            continue;

        switch(job->intervalType)
        {
        case tiBackupJobInterval::DAILY:
        {
            qDebug() << "daily::curTime::" << curDate.toString("hh:mm") << "::jobTime::" << job->intervalTime;
            if(curDate.toString("hh:mm") == job->intervalTime)
            {
                qInfo() << "Starting scheduled backup:" << job->name;
                manager->startBackup(job->name);
            }
            break;
        }
        case tiBackupJobInterval::WEEKLY:
        {
            qDebug() << "weekly::curTime::" << curDate.toString("hh:mm") << "::jobTime::" << job->intervalTime;
            if(curDate.toString("hh:mm") == job->intervalTime && (curDate.date().dayOfWeek()-1) == job->intervalDay)
            {
                qInfo() << "Starting scheduled backup:" << job->name;
                manager->startBackup(job->name);
            }
            break;
        }
        case tiBackupJobInterval::MONTHLY:
        {
            qDebug() << "monthly::curTime::" << curDate.toString("hh:mm") << "::jobTime::" << job->intervalTime;
            if(curDate.toString("hh:mm") == job->intervalTime && curDate.date().day() == job->intervalDay)
            {
                qInfo() << "Starting scheduled backup:" << job->name;
                manager->startBackup(job->name);
            }
            break;
        }
        case tiBackupJobInterval::NONE:
            break;
        }
    }
}
