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

#ifndef TIBACKUP_LOGGING_H
#define TIBACKUP_LOGGING_H

#include <QtGlobal>

class QMessageLogContext;
class QString;

namespace tibackup {

// Process-wide gate controlling whether QtDebugMsg lines are written to the log
// file. Read once at startup from main/debug and updated live when the web UI
// saves settings, so toggling debug needs no daemon restart.
void setDebugLogging(bool enabled);
bool debugLogging();

// Qt message handler: writes timestamped, level-tagged lines
//   "MMM d hh:mm:ss [LEVEL] <message>"   (LEVEL = DEBUG|INFO|WARN|ERROR|CRIT)
// to <paths/logs>/tibackup.log. The level token is what the web UI parses to
// filter the daemon log. Install via qInstallMessageHandler().
void logMessageOutput(QtMsgType type, const QMessageLogContext &ctx, const QString &str);

} // namespace tibackup

#endif // TIBACKUP_LOGGING_H
