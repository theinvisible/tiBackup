#ifndef DISKOBSERVER_H
#define DISKOBSERVER_H

#include <QObject>

#include "tibackuplib.h"

class DiskMain : public QObject
{
    Q_OBJECT
public:
    explicit DiskMain(QObject *parent = 0);

signals:

public slots:
    void onDiskRemoved(DeviceDisk *disk);
    void onDiskAdded(DeviceDisk *disk);

private:

};

#endif // DISKOBSERVER_H
