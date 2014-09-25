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

#include <ticonf.h>
#include <diskmain.h>

QFile *tibackupLog = 0;

void logMessageOutput(QtMsgType type, const char *msg)
{
    if(tibackupLog == 0)
    {

        tiConfMain main_settings;
        tibackupLog = new QFile(QString("%1/tibackup.log").arg(main_settings.getValue("paths/logs").toString()));
        tibackupLog->open(QIODevice::Append | QIODevice::Text);
    }

    QTextStream out(tibackupLog);
    QDateTime currentDate = QDateTime::currentDateTime();

    switch (type) {
    case QtDebugMsg:
        out << currentDate.toString("MMM d hh:mm:ss").toStdString().c_str() << " tiBackup::Debug: " << msg << "\n";
        break;
    case QtWarningMsg:
        out << currentDate.toString("MMM d hh:mm:ss").toStdString().c_str() << " tiBackup::Warning: " << msg << "\n";
        break;
    case QtCriticalMsg:
        out << currentDate.toString("MMM d hh:mm:ss").toStdString().c_str() << " tiBackup::Critical: " << msg << "\n";
        break;
    case QtFatalMsg:
        out << currentDate.toString("MMM d hh:mm:ss").toStdString().c_str() << " tiBackup::Fatal: " << msg << "\n";
        tibackupLog->flush();
        abort();
    }

    tibackupLog->flush();
}

int main(int argc, char *argv[])
{
    qInstallMsgHandler(logMessageOutput);
    QCoreApplication a(argc, argv);

    DiskMain *observer = new DiskMain();

    return a.exec();
}

