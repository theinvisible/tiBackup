#ifndef HTTPSERVE_H
#define HTTPSERVE_H

#include <QObject>

class httpserve : public QObject
{
    Q_OBJECT
private:
    QString searchConfigFile();
public:
    explicit httpserve(QObject *parent = nullptr);

signals:

};

#endif // HTTPSERVE_H
