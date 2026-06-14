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

#ifndef JSONMAP_H
#define JSONMAP_H

#include <QJsonObject>

class tiBackupJob;
class PBServer;
class DeviceDisk;
class DeviceDiskPartition;

// Maps the lib data objects to/from JSON. Field names and the int<->enum casts
// mirror ticonf.cpp exactly, so the web layer persists identical .conf files.
namespace jsonmap {

QJsonObject jobToJson(const tiBackupJob &job);
tiBackupJob jobFromJson(const QJsonObject &o);

// Secrets (password/keypass) are only serialized when includeSecrets is true,
// which is never the case for GET responses.
QJsonObject pbServerToJson(const PBServer &srv, bool includeSecrets = false);
PBServer    pbServerFromJson(const QJsonObject &o);

QJsonObject partitionToJson(const DeviceDiskPartition &p);
QJsonObject diskToJson(const DeviceDisk &d);

} // namespace jsonmap

#endif // JSONMAP_H
