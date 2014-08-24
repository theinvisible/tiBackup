#include <QCoreApplication>

#include <diskmain.h>

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    DiskMain *observer = new DiskMain();

    return a.exec();
}

