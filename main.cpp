#include <QCoreApplication>

#include <diskobserver.h>

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    DiskObserver *observer = new DiskObserver();

    return a.exec();
}

