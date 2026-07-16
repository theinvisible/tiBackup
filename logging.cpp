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

#include "logging.h"

#include <atomic>
#include <cstdlib>

#include <QFile>
#include <QMutex>
#include <QString>
#include <QTextStream>
#include <QDateTime>

#include "ticonf.h"

namespace {

// Default off: the daemon stays quiet out of the box. main() seeds this from
// main/debug before installing the handler.
std::atomic<bool> g_debugLogging{false};

// Opened once on first message and kept open for the process lifetime, so we
// don't re-read the config / reopen the file on every single log line.
QFile *g_logFile = nullptr;

// The Qt message handler is invoked concurrently from the main thread, the
// disk-watcher thread and every backup-worker thread. Serialise the lazy file
// creation and each write+flush so lines are not torn/interleaved and g_logFile
// is not double-opened. Non-recursive is safe: the one-time init constructs
// tiConfMain, which only emits a Qt log message on a missing config file - a
// startup-only, single-threaded path that never overlaps a worker.
QMutex g_logMutex;

const char *levelTag(QtMsgType type)
{
    switch(type)
    {
    case QtDebugMsg:    return "DEBUG";
    case QtInfoMsg:     return "INFO";
    case QtWarningMsg:  return "WARN";
    case QtCriticalMsg: return "ERROR";
    case QtFatalMsg:    return "CRIT";
    }
    return "INFO";
}

} // namespace

namespace tibackup {

void setDebugLogging(bool enabled)
{
    g_debugLogging.store(enabled, std::memory_order_relaxed);
}

bool debugLogging()
{
    return g_debugLogging.load(std::memory_order_relaxed);
}

void logMessageOutput(QtMsgType type, const QMessageLogContext &, const QString &str)
{
    // Debug lines are only persisted when explicitly enabled; everything else
    // (info/warning/error/critical) is always written.
    if(type == QtDebugMsg && !g_debugLogging.load(std::memory_order_relaxed))
        return;

    {
        QMutexLocker lock(&g_logMutex);

        if(g_logFile == nullptr)
        {
            tiConfMain main_settings;
            g_logFile = new QFile(QString("%1/tibackup.log").arg(main_settings.getValue("paths/logs").toString()));
            g_logFile->open(QIODevice::Append | QIODevice::Text);
        }

        QTextStream out(g_logFile);
        out << QDateTime::currentDateTime().toString("MMM d hh:mm:ss")
            << " [" << levelTag(type) << "] " << str << "\n";
        g_logFile->flush();
    }

    // Release the lock before aborting so a concurrent handler isn't left
    // holding a destroyed mutex mid-write (abort() never returns anyway).
    if(type == QtFatalMsg)
        std::abort();
}

} // namespace tibackup
