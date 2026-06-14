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

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QStringList>

#include <ticonf.h>
#include <diskmain.h>
#include "webserver/webserver.h"
#include "webserver/auth/passwordhash.h"

QFile *tibackupLog = 0;

void logMessageOutput(QtMsgType type, const QMessageLogContext &, const QString & str)
{
    tiConfMain main_settings;

    if(tibackupLog == 0)
    {
        tibackupLog = new QFile(QString("%1/tibackup.log").arg(main_settings.getValue("paths/logs").toString()));
        tibackupLog->open(QIODevice::Append | QIODevice::Text);
    }

    bool tidebug = main_settings.getValue("main/debug").toBool();

    QTextStream out(tibackupLog);
    QDateTime currentDate = QDateTime::currentDateTime();

    switch (type) {
    case QtDebugMsg:
        if(tidebug == true)
            out << currentDate.toString("MMM d hh:mm:ss").toStdString().c_str() << " tiBackup::Debug: " << str << "\n";
        break;
    case QtInfoMsg:
        out << currentDate.toString("MMM d hh:mm:ss").toStdString().c_str() << " tiBackup::Info: " << str << "\n";
        break;
    case QtWarningMsg:
        out << currentDate.toString("MMM d hh:mm:ss").toStdString().c_str() << " tiBackup::Warning: " << str << "\n";
        break;
    case QtCriticalMsg:
        out << currentDate.toString("MMM d hh:mm:ss").toStdString().c_str() << " tiBackup::Critical: " << str << "\n";
        break;
    case QtFatalMsg:
        out << currentDate.toString("MMM d hh:mm:ss").toStdString().c_str() << " tiBackup::Fatal: " << str << "\n";
        tibackupLog->flush();
        abort();
    }

    tibackupLog->flush();
}

// Headless provisioning of the web admin password: `tiBackup --set-web-password`
// (run as root). Reads the new password from stdin and stores a salted hash in
// /etc/tibackup/main.conf, then exits without starting the daemon.
static int setWebPassword()
{
    QTextStream err(stderr);
    QTextStream in(stdin);

    err << "Enter new tiBackup web admin password: ";
    err.flush();
    const QString pw = in.readLine();

    if(pw.size() < 8)
    {
        err << "Password must be at least 8 characters.\n";
        return 1;
    }

    tiConfMain cfg;
    const QByteArray salt = passwordhash::generateSalt();
    cfg.setValue("web/salt", QString::fromLatin1(salt));
    cfg.setValue("web/passhash", passwordhash::hash(pw, salt));
    cfg.sync();

    err << "Web admin password updated.\n";
    return 0;
}

int main(int argc, char *argv[])
{
    qInstallMessageHandler(logMessageOutput);

    qRegisterMetaType<DeviceDiskPartition>("DeviceDiskPartition");

    QCoreApplication a(argc, argv);

    if(a.arguments().contains(QStringLiteral("--set-web-password")))
        return setWebPassword();

    // DiskMain owns the backupManager that the scheduler/hotplug paths use; the
    // web UI drives manual backups and live status from that SAME instance.
    DiskMain observer;
    WebServer web(observer.getManager(), &a);

    return a.exec();
}

