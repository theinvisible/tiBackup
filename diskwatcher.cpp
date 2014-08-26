#include "diskwatcher.h"

#include <QDebug>

DiskWatcher::DiskWatcher(QObject *parent) : QObject(parent)
{
}

void DiskWatcher::process()
{
    // Work is here
    tiBackupDiskObserver *obs = new tiBackupDiskObserver();
    QObject::connect(obs, SIGNAL(diskRemoved(DeviceDisk*)), this, SLOT(onDiskRemoved(DeviceDisk*)));
    QObject::connect(obs, SIGNAL(diskAdded(DeviceDisk*)), this, SLOT(onDiskAdded(DeviceDisk*)));
    obs->start();

    emit finished();
}

void DiskWatcher::onDiskRemoved(DeviceDisk *disk)
{
    qDebug() << "disk removed 111 ->" << disk->name;

    emit diskRemoved(disk);
}

void DiskWatcher::onDiskAdded(DeviceDisk *disk)
{
    qDebug() << "disk added 111 ->" << disk->name;

    emit diskAdded(disk);
}
