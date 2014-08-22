#ifndef DISKOBSERVER_H
#define DISKOBSERVER_H

#include <QObject>

#include <libudev.h>
#include <lib/devicedisk.h>

class DiskObserver : public QObject
{
    Q_OBJECT
public:
    explicit DiskObserver(QObject *parent = 0);

signals:

public slots:

private:
    QList<DeviceDisk> getAttachedDisks();
    bool isDeviceUSB(struct udev_device *device);
    void print_device(struct udev_device *device, const char *source);

};

#endif // DISKOBSERVER_H
