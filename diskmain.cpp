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
#include <QSettings>

#include "config.h"
#include "tibackupdiskobserver.h"
#include "diskwatcher.h"
#include "ticonf.h"
#include "tibackupscheduler.h"
#include "workers/tibackupjobworker.h"

DiskMain::DiskMain(QObject *parent) : QObject(parent)
{
    std::cout << "Starting tiBackup Server " << QString(tibackup_config::version).toStdString() << std::endl;

    manager = new backupManager(this);

    // The disk watcher blocks forever polling udev (DiskWatcher::process ->
    // tiBackupDiskObserver::start), so it intentionally lives for the whole process
    // lifetime; there is no finished()/cleanup path (the previous finished()-driven
    // quit/deleteLater connects were dead code and have been removed).
    QThread* thread = new QThread;
    DiskWatcher* worker = new DiskWatcher();
    worker->moveToThread(thread);
    connect(thread, SIGNAL(started()), worker, SLOT(process()));
    connect(worker, &DiskWatcher::diskRemoved, this, &DiskMain::onDiskRemoved);
    connect(worker, &DiskWatcher::diskAdded, this, &DiskMain::onDiskAdded);
    thread->start();

    // We start a timer for task observation
    QTimer *timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(onTaskCheck()));
    timer->start(1000*60);

    onTaskCheck();
}

void DiskMain::onDiskRemoved(const DeviceDisk &disk)
{
    qInfo() << "Disk removed:" << disk.name;
}

void DiskMain::onDiskAdded(DeviceDisk disk)
{
    qInfo() << "Disk added:" << disk.name;

    disk.readPartitions();
    qDebug() << "DiskMain::onDiskAdded() -> diskpartcount::" << disk.partitions.count();
    for(int i=0; i < disk.partitions.count(); i++)
    {
        DeviceDiskPartition part = disk.partitions.at(i);
        qDebug() << "DiskMain::onDiskAdded() -> disk partition added::" << part.uuid;

        tiConfBackupJobs objjobs;
        const QList<tiBackupJob> jobs = objjobs.getJobsByUuid(part.uuid);
        for(const tiBackupJob &job : jobs)
        {
            qDebug() << "DiskMain::onDiskAdded() -> job found for uuid::" << job.name;

            if(job.start_backup_on_hotplug == false)
                continue;

            qInfo() << "Starting backup on hotplug:" << job.name;
            manager->startBackup(job.name);
        }
    }
}

void DiskMain::onTaskCheck()
{
    const QDateTime now = QDateTime::currentDateTime();

    qDebug() << "DiskMain::onTaskCheck()-> " << now.toString("MMM d hh:mm:ss");

    tiConfBackupJobs objjobs;
    const QList<tiBackupJob> jobs = objjobs.getJobs();

    // Per-job last-run timestamps live in a root-only sidecar next to main.conf,
    // NOT in the job .conf (saveBackupJob rewrites that on every web edit, which
    // would wipe the state). This gives strict-slot firing: each slot runs at most
    // once (no double-fire on restart / fast completion), and a run missed because
    // the daemon was down over its slot is skipped rather than resurrected. The
    // firing decision is the pure tiBackupScheduler::shouldRun().
    QSettings state(tibackup_config::schedulerStateFile(), QSettings::IniFormat);

    for(const tiBackupJob &job : jobs)
    {
        if(job.intervalType == tiBackupJobInterval::NONE)
            continue;

        const QString key = QString("%1/last_run").arg(job.name);
        const qint64 lastRun = state.value(key).toLongLong();

        if(tiBackupScheduler::shouldRun(job, now, lastRun))
        {
            qInfo() << "Starting scheduled backup:" << job.name;
            // Record the run BEFORE launching so a second tick within the same
            // grace window can't re-fire the job.
            state.setValue(key, now.toSecsSinceEpoch());
            state.sync();
            manager->startBackup(job.name);
        }
    }
}
