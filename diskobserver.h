#ifndef DISKOBSERVER_H
#define DISKOBSERVER_H

#include <QObject>

class DiskObserver : public QObject
{
    Q_OBJECT
public:
    explicit DiskObserver(QObject *parent = 0);

signals:

public slots:

};

#endif // DISKOBSERVER_H
