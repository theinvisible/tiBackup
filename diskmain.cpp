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

// _GNU_SOURCE is required for struct ucred / SO_PEERCRED / getgrouplist (glibc)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QVector>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <grp.h>
#include <pwd.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

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

    // Start API Server
    QLocalServer::removeServer(tibackup_config::api_sock_name);
    apiServer = new QLocalServer(this);
    apiServer->setSocketOptions(QLocalServer::UserAccessOption | QLocalServer::GroupAccessOption);
    connect(apiServer, SIGNAL(newConnection()), this, SLOT(onAPIConnected()));
    qDebug() << "DiskMain::DiskMain() on apiServer->listen::starting server::" << tibackup_config::api_sock_name;
    if(!apiServer->listen(tibackup_config::api_sock_name))
    {
        qDebug() << "DiskMain::DiskMain() on apiServer->listen::" << apiServer->errorString();
    }
    else
    {
        // Restrict the IPC socket to the "tibackup" group (root + group members only).
        const QByteArray sockPath = apiServer->fullServerName().toLocal8Bit();
        struct group *grp = getgrnam("tibackup");
        if(grp != nullptr)
        {
            if(chown(sockPath.constData(), static_cast<uid_t>(-1), grp->gr_gid) != 0)
                qWarning() << "DiskMain::DiskMain() could not chgrp IPC socket:" << strerror(errno);
            chmod(sockPath.constData(), 0660);
        }
        else
        {
            qWarning() << "DiskMain::DiskMain() group 'tibackup' not found; IPC socket left owner-only";
        }
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

        if(job->intervalType == tiBackupJobInterval::NONE)
            continue;

        switch(job->intervalType)
        {
        case tiBackupJobInterval::DAILY:
        {
            qDebug() << "daily::curTime::" << curDate.toString("hh:mm") << "::jobTime::" << job->intervalTime;
            if(curDate.toString("hh:mm") == job->intervalTime)
            {
                qDebug() << "we start task for backup id " << job->name;
                manager->startBackup(job->name);
            }
            break;
        }
        case tiBackupJobInterval::WEEKLY:
        {
            qDebug() << "monthly::curTime::" << curDate.toString("hh:mm") << "::jobTime::" << job->intervalTime;
            if(curDate.toString("hh:mm") == job->intervalTime && (curDate.date().dayOfWeek()-1) == job->intervalDay)
            {
                qDebug() << "we start task for backup id " << job->name;
                manager->startBackup(job->name);
            }
            break;
        }
        case tiBackupJobInterval::MONTHLY:
        {
            qDebug() << "monthly::curTime::" << curDate.toString("hh:mm") << "::jobTime::" << job->intervalTime;
            if(curDate.toString("hh:mm") == job->intervalTime && curDate.date().day() == job->intervalDay)
            {
                qDebug() << "we start task for backup id " << job->name;
                manager->startBackup(job->name);
            }
            break;
        }
        case tiBackupJobInterval::NONE:
            break;
        }
    }
}

static void writeAck(QLocalSocket *client, qint32 code)
{
    QByteArray block;
    QDataStream out(&block, QIODevice::WriteOnly);
    out.setVersion(tibackup_config::ipc_version);
    out << code;
    client->write(block);
    client->flush();
}

// Authorize the connecting peer via SO_PEERCRED: root and members of the
// "tibackup" group are allowed, everyone else is rejected.
static bool peerAuthorized(QLocalSocket *client)
{
    struct ucred cred;
    socklen_t len = sizeof(cred);
    int fd = static_cast<int>(client->socketDescriptor());
    if(fd < 0)
        return false;
    if(getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0)
    {
        qWarning() << "peerAuthorized() SO_PEERCRED failed:" << strerror(errno);
        return false;
    }
    if(cred.uid == 0)
        return true;

    struct group *grp = getgrnam("tibackup");
    if(grp == nullptr)
        return false;
    if(cred.gid == grp->gr_gid)
        return true;

    struct passwd *pw = getpwuid(cred.uid);
    if(pw != nullptr)
    {
        int ngroups = 0;
        getgrouplist(pw->pw_name, pw->pw_gid, nullptr, &ngroups);
        if(ngroups > 0)
        {
            QVector<gid_t> groups(ngroups);
            if(getgrouplist(pw->pw_name, pw->pw_gid, groups.data(), &ngroups) > 0)
            {
                for(int i = 0; i < ngroups; ++i)
                    if(groups[i] == grp->gr_gid)
                        return true;
            }
        }
    }
    return false;
}

// Write a pre/post-backup script, confined to the configured paths/scripts dir.
static bool saveScriptConfined(const QString &path, const QString &content)
{
    tiConfMain main;
    const QString base = QDir::cleanPath(QDir(main.getValue("paths/scripts").toString()).absolutePath());
    const QString target = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if(base.isEmpty() || (target != base && !target.startsWith(base + "/")))
    {
        qWarning() << "saveScriptConfined() rejected path outside" << base << ":" << target;
        return false;
    }

    QFile f(target);
    if(!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream out(&f);
    out << content;
    f.close();
    return true;
}

void DiskMain::onAPIConnected()
{
    qDebug() << "DiskMain::onAPIConnected()";

    if(apiServer->hasPendingConnections())
    {
        QLocalSocket *client = apiServer->nextPendingConnection();
        connect(client, SIGNAL(disconnected()), client, SLOT(deleteLater()));

        if(!peerAuthorized(client))
        {
            qWarning() << "DiskMain::onAPIConnected() rejected unauthorized IPC peer";
            client->abort();
            return;
        }

        client->waitForReadyRead();

        QHash<int, QString> apiData;
        QDataStream in(client);
        in.setVersion(tibackup_config::ipc_version);
        in >> apiData;

        //QByteArray tmp = client->readAll();
        qDebug() << "client api command::" << apiData;
        client->flush();

        if(apiData[tiBackupApi::API_VAR::API_VAR_CMD] == QString::number(tiBackupApi::API_CMD_START))
        {
            tiConfBackupJobs objjobs;
            objjobs.readBackupJobs();
            tiBackupJob* job = objjobs.getJobByName(apiData[tiBackupApi::API_VAR_BACKUPJOB]);
            manager->startBackup(job->name);
        }
        else if(apiData[tiBackupApi::API_VAR_CMD] == QString::number(tiBackupApi::API_CMD_DISK_GET_PARTITIONS))
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
        else if(apiData[tiBackupApi::API_VAR_CMD] == QString::number(tiBackupApi::API_CMD_DISK_GET_PARTITION_BY_DEVNAME_UUID))
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
        else if(apiData[tiBackupApi::API_VAR_CMD] == QString::number(tiBackupApi::API_CMD_DISK_GET_PARTITION_BY_UUID))
        {
            DeviceDiskPartition part = TiBackupLib::getPartitionByUUID(apiData[tiBackupApi::API_VAR_PART_UUID]);

            QByteArray block;
            QDataStream out(&block, QIODevice::WriteOnly);
            out.setVersion(tibackup_config::ipc_version);
            out << part;

            client->write(block);
            client->flush();
        }
        else if(apiData[tiBackupApi::API_VAR_CMD] == QString::number(tiBackupApi::API_CMD_PART_MOUNT))
        {
            DeviceDiskPartition part = TiBackupLib::getPartitionByUUID(apiData[tiBackupApi::API_VAR_PART_UUID]);

            tiBackupJob job;
            job.encLUKSType = static_cast<tiBackupEncLUKS>(apiData[tiBackupApi::API_VAR_JOB_LUKS_TYPE].toInt());
            job.encLUKSFilePath = apiData[tiBackupApi::API_VAR_JOB_LUKS_FILEPATH];

            TiBackupLib lib;
            lib.mountPartition(&part, &job);
        }
        else if(apiData[tiBackupApi::API_VAR_CMD] == QString::number(tiBackupApi::API_CMD_BACKUP_STATUS))
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
        else if(apiData[tiBackupApi::API_VAR_CMD] == QString::number(tiBackupApi::API_CMD_CONF_SET_MAIN))
        {
            QHash<QString, QString> values;
            in >> values;

            tiConfMain main;
            for(auto it = values.cbegin(); it != values.cend(); ++it)
                main.setValue(it.key(), it.value());
            main.sync();

            writeAck(client, tiBackupApi::API_RESULT_OK);
        }
        else if(apiData[tiBackupApi::API_VAR_CMD] == QString::number(tiBackupApi::API_CMD_JOB_SAVE))
        {
            tiBackupJob job;
            in >> job;

            tiConfBackupJobs jobs;
            jobs.saveBackupJob(job);

            writeAck(client, tiBackupApi::API_RESULT_OK);
        }
        else if(apiData[tiBackupApi::API_VAR_CMD] == QString::number(tiBackupApi::API_CMD_JOB_DELETE))
        {
            tiConfBackupJobs jobs;
            jobs.removeJobByName(apiData[tiBackupApi::API_VAR_BACKUPJOB]);

            writeAck(client, tiBackupApi::API_RESULT_OK);
        }
        else if(apiData[tiBackupApi::API_VAR_CMD] == QString::number(tiBackupApi::API_CMD_JOB_RENAME))
        {
            tiConfBackupJobs jobs;
            jobs.renameJob(apiData[tiBackupApi::API_VAR_JOB_OLDNAME], apiData[tiBackupApi::API_VAR_JOB_NEWNAME]);

            writeAck(client, tiBackupApi::API_RESULT_OK);
        }
        else if(apiData[tiBackupApi::API_VAR_CMD] == QString::number(tiBackupApi::API_CMD_PBS_SAVE))
        {
            PBServer p;
            in >> p;

            tiConfPBServers::instance()->saveItem(p);

            writeAck(client, tiBackupApi::API_RESULT_OK);
        }
        else if(apiData[tiBackupApi::API_VAR_CMD] == QString::number(tiBackupApi::API_CMD_PBS_DELETE))
        {
            tiConfPBServers::instance()->removeItemByUuid(apiData[tiBackupApi::API_VAR_PBS_UUID]);

            writeAck(client, tiBackupApi::API_RESULT_OK);
        }
        else if(apiData[tiBackupApi::API_VAR_CMD] == QString::number(tiBackupApi::API_CMD_SCRIPT_SAVE))
        {
            QString content;
            in >> content;

            bool ok = saveScriptConfined(apiData[tiBackupApi::API_VAR_SCRIPT_PATH], content);
            writeAck(client, ok ? tiBackupApi::API_RESULT_OK : tiBackupApi::API_RESULT_ERROR);
        }

        client->disconnectFromServer();
    }
}
