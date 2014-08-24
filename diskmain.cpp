#include "diskmain.h"

#include <QDebug>
#include <QThread>

#include "tibackupdiskobserver.h"
#include "diskwatcher.h"

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
    thread->start();
}
