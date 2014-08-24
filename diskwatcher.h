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
