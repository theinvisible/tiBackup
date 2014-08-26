#include "diskmain.h"

#include <QDebug>
#include <QThread>

#include "tibackupdiskobserver.h"
#include "diskwatcher.h"
#include "ticonf.h"

DiskMain::DiskMain(QObject *parent) : QObject(parent)
{
    TiBackupLib lib;
    DeviceDisk disk = lib.getAttachedDisks().at(0);

    qDebug() << "disk name1:" << disk.devname;

    disk.readPartitions();

    qDebug() << "disk name2:" << disk.devname;
    qDebug() << "part found:" << disk.partitions.count();

    for(int i=0; i < disk.partitions.count(); i++)
    {
        DeviceDiskPartition diskpart = disk.partitions.at(i);
        qDebug() << "part" << i << "=" << diskpart.name;
    }

    //tiBackupDiskObserver *obs = new tiBackupDiskObserver();

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
}

void DiskMain::onDiskRemoved(DeviceDisk *disk)
{
    qDebug() << "disk name22222222222222222222222222:" << disk->name;
}

void DiskMain::onDiskAdded(DeviceDisk *disk)
{
    qDebug() << "disk name3333333333333333333333333:" << disk->name;

    TiBackupLib backlib;

    disk->readPartitions();
    qDebug() << "diskpartcount::" << disk->partitions.count();
    for(int i=0; i < disk->partitions.count(); i++)
    {
        DeviceDiskPartition part = disk->partitions.at(i);
        qDebug() << "disk partition added::" << part.uuid;

        tiConfBackupJobs objjobs;
        QList<tiBackupJob*> jobs = objjobs.getJobsByUuid(part.uuid);
        for(int j=0; j < jobs.count(); j++)
        {
            tiBackupJob *job = jobs.at(j);
            qDebug() << "job found for uuid::" << job->name;

            // Mount volume
            backlib.mountPartition(&part);

            // Do the backup stuff

            // Umount volume
            backlib.umountPartition(&part);

            // Send message with status
        }
    }
}
