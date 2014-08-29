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
