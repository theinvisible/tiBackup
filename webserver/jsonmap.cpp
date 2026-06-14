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

#include "webserver/jsonmap.h"

#include <QJsonArray>

#include "obj/tibackupjob.h"
#include "obj/pbserver.h"
#include "obj/devicedisk.h"

QJsonObject jsonmap::jobToJson(const tiBackupJob &job)
{
    QJsonObject o;
    o["name"]           = job.name;
    o["device"]         = job.device;
    o["partition_uuid"] = job.partition_uuid;

    QJsonArray dirs;
    for(auto it = job.backupdirs.cbegin(); it != job.backupdirs.cend(); ++it)
    {
        QJsonObject d;
        d["source"] = it.key();
        d["dest"]   = it.value();
        dirs.append(d);
    }
    o["backupdirs"] = dirs;

    o["delete_add_file_on_dest"] = job.delete_add_file_on_dest;
    o["start_backup_on_hotplug"] = job.start_backup_on_hotplug;
    o["save_log"]                = job.save_log;
    o["compare_via_checksum"]    = job.compare_via_checksum;

    o["notify"]           = job.notify;
    o["notifyRecipients"] = job.notifyRecipients;

    o["scriptBeforeBackup"] = job.scriptBeforeBackup;
    o["scriptAfterBackup"]  = job.scriptAfterBackup;

    o["intervalType"] = static_cast<int>(job.intervalType);
    o["intervalTime"] = job.intervalTime;
    o["intervalDay"]  = job.intervalDay;

    o["encLUKSType"]     = static_cast<int>(job.encLUKSType);
    o["encLUKSFilePath"] = job.encLUKSFilePath;

    o["pbs"]                = job.pbs;
    o["pbs_server_uuid"]    = job.pbs_server_uuid;
    o["pbs_server_storage"] = job.pbs_server_storage;
    QJsonArray ids;
    for(const QString &id : job.pbs_backup_ids)
        ids.append(id);
    o["pbs_backup_ids"] = ids;
    o["pbs_dest_folder"] = job.pbs_dest_folder;

    return o;
}

tiBackupJob jsonmap::jobFromJson(const QJsonObject &o)
{
    tiBackupJob job;
    job.name           = o["name"].toString();
    job.device         = o["device"].toString();
    job.partition_uuid = o["partition_uuid"].toString();

    const QJsonArray dirs = o["backupdirs"].toArray();
    for(const QJsonValue &v : dirs)
    {
        const QJsonObject d = v.toObject();
        job.backupdirs.insert(d["source"].toString(), d["dest"].toString());
    }

    job.delete_add_file_on_dest = o["delete_add_file_on_dest"].toBool();
    job.start_backup_on_hotplug = o["start_backup_on_hotplug"].toBool();
    job.save_log                = o["save_log"].toBool();
    job.compare_via_checksum    = o["compare_via_checksum"].toBool();

    job.notify           = o["notify"].toBool();
    job.notifyRecipients = o["notifyRecipients"].toString();

    job.scriptBeforeBackup = o["scriptBeforeBackup"].toString();
    job.scriptAfterBackup  = o["scriptAfterBackup"].toString();

    job.intervalType = static_cast<tiBackupJobInterval>(o["intervalType"].toInt());
    job.intervalTime = o["intervalTime"].toString();
    job.intervalDay  = o["intervalDay"].toInt();

    job.encLUKSType     = static_cast<tiBackupEncLUKS>(o["encLUKSType"].toInt());
    job.encLUKSFilePath = o["encLUKSFilePath"].toString();

    job.pbs                = o["pbs"].toBool();
    job.pbs_server_uuid    = o["pbs_server_uuid"].toString();
    job.pbs_server_storage = o["pbs_server_storage"].toString();
    const QJsonArray ids = o["pbs_backup_ids"].toArray();
    for(const QJsonValue &v : ids)
        job.pbs_backup_ids.append(v.toString());
    job.pbs_dest_folder = o["pbs_dest_folder"].toString();

    return job;
}

QJsonObject jsonmap::pbServerToJson(const PBServer &srv, bool includeSecrets)
{
    QJsonObject o;
    o["uuid"]        = srv.uuid;
    o["name"]        = srv.name;
    o["host"]        = srv.host;
    o["port"]        = static_cast<int>(srv.port);
    o["username"]    = srv.username;
    o["fingerprint"] = srv.fingerprint;
    o["keyfile"]     = srv.keyfile;
    if(includeSecrets)
    {
        o["password"] = srv.password;
        o["keypass"]  = srv.keypass;
    }
    return o;
}

PBServer jsonmap::pbServerFromJson(const QJsonObject &o)
{
    PBServer srv; // ctor generates a fresh uuid; keep it unless one is supplied
    const QString uuid = o["uuid"].toString();
    if(!uuid.isEmpty())
        srv.uuid = uuid;

    srv.name        = o["name"].toString();
    srv.host        = o["host"].toString();
    srv.port        = static_cast<uint>(o["port"].toInt(8007));
    srv.username    = o["username"].toString();
    srv.password    = o["password"].toString();
    srv.fingerprint = o["fingerprint"].toString();
    srv.keyfile     = o["keyfile"].toString();
    srv.keypass     = o["keypass"].toString();
    return srv;
}

QJsonObject jsonmap::partitionToJson(const DeviceDiskPartition &p)
{
    QJsonObject o;
    o["name"]  = p.name;
    o["uuid"]  = p.uuid;
    o["label"] = p.label;
    o["type"]  = p.type;
    return o;
}

QJsonObject jsonmap::diskToJson(const DeviceDisk &d)
{
    QJsonObject o;
    o["name"]    = d.name;
    o["uuid"]    = d.uuid;
    o["devname"] = d.devname;
    o["devtype"] = d.devtype;
    o["vendor"]  = d.vendor;
    o["model"]   = d.model;

    QJsonArray parts;
    for(const DeviceDiskPartition &p : d.partitions)
        parts.append(partitionToJson(p));
    o["partitions"] = parts;

    return o;
}
