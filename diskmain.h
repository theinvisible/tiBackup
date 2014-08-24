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

private:

};

#endif // DISKOBSERVER_H
