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
#include <QLocalSocket>

#include "config.h"
#include "tibackupdiskobserver.h"
#include "diskwatcher.h"
#include "ticonf.h"
#include "tibackupapi.h"
#include "workers/tibackupjobworker.h"
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <QDataStream>
#endif

DiskMain::DiskMain(QObject *parent) : QObject(parent)
{
    std::cout << "Starting tiBackup Server " << tibackup_config::version << std::endl;

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

    // Start API Server
    QLocalServer::removeServer(tibackup_config::api_sock_name);
    apiServer = new QLocalServer(this);
    apiServer->setSocketOptions(QLocalServer::WorldAccessOption);
    connect(apiServer, SIGNAL(newConnection()), this, SLOT(onAPIConnected()));
    qDebug() << "DiskMain::DiskMain() on apiServer->listen::starting server::" << tibackup_config::api_sock_name;
    if(!apiServer->listen(tibackup_config::api_sock_name))
    {
        qDebug() << "DiskMain::DiskMain() on apiServer->listen::" << apiServer->errorString();
    }

    onTaskCheck();
}

void DiskMain::onDiskRemoved(DeviceDisk *disk)
{
    qDebug() << "DiskMain::onDiskRemoved()" << disk->name;
}

void DiskMain::onDiskAdded(DeviceDisk *disk)
{
    qDebug() << "DiskMain::onDiskAdded() -> " << disk->name;

    TiBackupLib backlib;

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

            //job->startBackup(&part);
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

        if(job->intervalType == tiBackupJobIntervalNONE)
            continue;

        switch(job->intervalType)
        {
        case tiBackupJobIntervalDAILY:
        {
            qDebug() << "daily::curTime::" << curDate.toString("hh:mm") << "::jobTime::" << job->intervalTime;
            if(curDate.toString("hh:mm") == job->intervalTime)
            {
                qDebug() << "we start task for backup id " << job->name;
                manager->startBackup(job->name);
            }
            break;
        }
        case tiBackupJobIntervalWEEKLY:
        {
            qDebug() << "monthly::curTime::" << curDate.toString("hh:mm") << "::jobTime::" << job->intervalTime;
            if(curDate.toString("hh:mm") == job->intervalTime && (curDate.date().dayOfWeek()-1) == job->intervalDay)
            {
                qDebug() << "we start task for backup id " << job->name;
                manager->startBackup(job->name);
            }
            break;
        }
        case tiBackupJobIntervalMONTHLY:
        {
            qDebug() << "monthly::curTime::" << curDate.toString("hh:mm") << "::jobTime::" << job->intervalTime;
            if(curDate.toString("hh:mm") == job->intervalTime && curDate.date().day() == job->intervalDay)
            {
                qDebug() << "we start task for backup id " << job->name;
                manager->startBackup(job->name);
            }
            break;
        }
        case tiBackupJobIntervalNONE:
            break;
        }
    }
}

void DiskMain::onAPIConnected()
{
    qDebug() << "DiskMain::onAPIConnected()";

    if(apiServer->hasPendingConnections())
    {
        QLocalSocket *client = apiServer->nextPendingConnection();
        connect(client, SIGNAL(disconnected()), client, SLOT(deleteLater()));
        client->waitForReadyRead();

        QHash<int, QString> apiData;
        QDataStream in(client);
        in.setVersion(tibackup_config::ipc_version);
        in >> apiData;

        //QByteArray tmp = client->readAll();
        qDebug() << "client api command::" << apiData;
        client->flush();

        if(apiData[tiBackupApi::API_VAR::API_VAR_CMD] == QString(tiBackupApi::API_CMD_START))
        {
            tiConfBackupJobs objjobs;
            objjobs.readBackupJobs();
            tiBackupJob* job = objjobs.getJobByName(apiData[tiBackupApi::API_VAR_BACKUPJOB]);
            manager->startBackup(job->name);
        }
        else if(apiData[tiBackupApi::API_VAR_CMD] == QString(tiBackupApi::API_CMD_DISK_GET_PARTITIONS))
        {
            DeviceDisk selDisk;
            selDisk.devname = apiData[tiBackupApi::API_VAR_DEVNAME];
            selDisk.readPartitions();

            QByteArray block;
            QDataStream out(&block, QIODevice::WriteOnly);
            out.setVersion(tibackup_config::ipc_version);
            out << selDisk.partitions;

            client->write(block);
            client->flush();
        }
        else if(apiData[tiBackupApi::API_VAR_CMD] == QString(tiBackupApi::API_CMD_DISK_GET_PARTITION_BY_DEVNAME_UUID))
        {
            DeviceDisk selDisk;
            selDisk.devname = apiData[tiBackupApi::API_VAR_DEVNAME];
            DeviceDiskPartition part = selDisk.getPartitionByUUID(apiData[tiBackupApi::API_VAR_PART_UUID]);

            QByteArray block;
            QDataStream out(&block, QIODevice::WriteOnly);
            out.setVersion(tibackup_config::ipc_version);
            out << part;

            client->write(block);
            client->flush();
        }
        else if(apiData[tiBackupApi::API_VAR_CMD] == QString(tiBackupApi::API_CMD_DISK_GET_PARTITION_BY_UUID))
        {
            DeviceDiskPartition part = TiBackupLib::getPartitionByUUID(apiData[tiBackupApi::API_VAR_PART_UUID]);

            QByteArray block;
            QDataStream out(&block, QIODevice::WriteOnly);
            out.setVersion(tibackup_config::ipc_version);
            out << part;

            client->write(block);
            client->flush();
        }
        else if(apiData[tiBackupApi::API_VAR_CMD] == QString(tiBackupApi::API_CMD_PART_MOUNT))
        {
            DeviceDiskPartition part = TiBackupLib::getPartitionByUUID(apiData[tiBackupApi::API_VAR_PART_UUID]);

            tiBackupJob job;
            job.encLUKSType = static_cast<tiBackupEncLUKS>(apiData[tiBackupApi::API_VAR_JOB_LUKS_TYPE].toInt());
            job.encLUKSFilePath = apiData[tiBackupApi::API_VAR_JOB_LUKS_FILEPATH];

            TiBackupLib lib;
            lib.mountPartition(&part, &job);
        }
        else if(apiData[tiBackupApi::API_VAR_CMD] == QString(tiBackupApi::API_CMD_BACKUP_STATUS))
        {
            if(apiData.contains(tiBackupApi::API_VAR_BACKUPJOB)) {
                backupManager::backupStatus stat = manager->getBackupStatus(apiData[tiBackupApi::API_VAR_BACKUPJOB]);
                QByteArray block;
                QDataStream out(&block, QIODevice::WriteOnly);
                out.setVersion(tibackup_config::ipc_version);
                out << stat;

                client->write(block);
                client->flush();
            } else {
                QHash<QString, backupManager::backupStatus> *stat = manager->getBackupStatus();
                QByteArray block;
                QDataStream out(&block, QIODevice::WriteOnly);
                out.setVersion(tibackup_config::ipc_version);
                out << *stat;

                client->write(block);
                client->flush();
            }
        }

        client->disconnectFromServer();
    }
}
