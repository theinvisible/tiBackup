#include "diskobserver.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <sys/time.h> //debug -> remove me
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

DiskObserver::DiskObserver(QObject *parent) : QObject(parent)
{
    getAttachedDisks();
}

QList<DeviceDisk> DiskObserver::getAttachedDisks()
{
    QList<DeviceDisk> disks;
    struct udev *udev;
    struct udev_monitor *udev_monitor = NULL;
    fd_set readfds;
    bool s_bSD_present = false;

    udev = udev_new();
    if (udev == NULL)
    {
        printf("udev_new FAILED \n");
    }

    struct udev_enumerate *enumerate;
    struct udev_list_entry *devices, *dev_list_entry;

    enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(enumerate, "block");
    udev_enumerate_scan_devices(enumerate);
    devices = udev_enumerate_get_list_entry(enumerate);

    udev_list_entry_foreach(dev_list_entry, devices)
    {
        struct udev_device *dev;
        const char* dev_path = udev_list_entry_get_name(dev_list_entry);
        dev = udev_device_new_from_syspath(udev, dev_path);

        if( isDeviceUSB(dev) )
        {
            udev_device_unref(dev);
            break;
        }

        udev_device_unref(dev);
    }
    udev_enumerate_unref(enumerate);

}

bool DiskObserver::isDeviceUSB(struct udev_device *device)
{
    bool retVal = false;
    struct udev_list_entry *list_entry = 0;
    struct udev_list_entry* model_entry = 0;

    //print_device(device, "UDEV");

    list_entry = udev_device_get_properties_list_entry(device);
    model_entry = udev_list_entry_get_by_name(list_entry, "ID_BUS");
    if( 0 != model_entry )
    {
        const char* szModelValue = udev_list_entry_get_value(model_entry);
        if( strcmp( szModelValue, "usb") == 0 )
        {
            //printf("Device is SD \n");
            retVal = true;

            print_device(device, "UDEV");
        }
    }
    return retVal;
}

void DiskObserver::print_device(struct udev_device *device, const char *source)
{
      struct timeval tv;
      struct timezone tz;

      gettimeofday(&tv, &tz);
      printf("%-6s[%llu.%06u] %-8s %s (%s)\n",
             source,
             (unsigned long long) tv.tv_sec, (unsigned int) tv.tv_usec,
             udev_device_get_action(device),
             udev_device_get_devpath(device),
             udev_device_get_subsystem(device));

            struct udev_list_entry *list_entry;

            udev_list_entry_foreach(list_entry, udev_device_get_properties_list_entry(device))
                  printf("%s=%s\n",
                         udev_list_entry_get_name(list_entry),
                         udev_list_entry_get_value(list_entry));
            printf("\n");

}
