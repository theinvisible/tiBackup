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

#ifndef DISKWATCHER_H
#define DISKWATCHER_H

#include <QObject>

#include "tibackupdiskobserver.h"

class DiskWatcher : public QObject
{
    Q_OBJECT
public:
    explicit DiskWatcher(QObject *parent = 0);

signals:
    void finished();
    void error(QString err);

    void diskRemoved(DeviceDisk *disk);
    void diskAdded(DeviceDisk *disk);

public slots:
    void process();

    void onDiskRemoved(DeviceDisk *disk);
    void onDiskAdded(DeviceDisk *disk);

};

#endif // DISKWATCHER_H
