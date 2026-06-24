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

#include "webserver/apirouter.h"

#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
#include <QHttpHeaders>   // added in Qt 6.7; older distros (e.g. Ubuntu 24.04 / Qt 6.4) lack it
#endif
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrlQuery>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QTextStream>
#include <QHostAddress>
#include <QRegularExpression>

#include "config.h"
#include "ticonf.h"
#include "logging.h"
#include "tibackuplib.h"
#include "backupmanager.h"
#include "pbsclient.h"
#include "obj/pbserver.h"
#include "webserver/jsonmap.h"
#include "webserver/auth/passwordhash.h"
#include "webserver/auth/sessionstore.h"

namespace {

using StatusCode = QHttpServerResponse::StatusCode;
using Method     = QHttpServerRequest::Method;

constexpr int kMaxLogTailBytes = 256 * 1024;

// Stop content-type sniffing on API responses (defence-in-depth against a
// browser treating a JSON body as something executable).
void addSecurityHeaders(QHttpServerResponse &resp)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    QHttpHeaders h = resp.headers();
    h.append("X-Content-Type-Options", "nosniff");
    resp.setHeaders(std::move(h));
#else
    resp.addHeader("X-Content-Type-Options", "nosniff");
#endif
}

// Names that are turned into "<dir>/<name>.conf" on disk (backup jobs). Reject
// anything with path separators or traversal so a crafted name can't escape its
// configured directory and write/delete arbitrary files as root.
bool validIdentifier(const QString &s)
{
    if(s.isEmpty() || s.size() > 128)
        return false;
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9._-]+$"));
    if(!re.match(s).hasMatch())
        return false;
    return s != QLatin1String(".") && s != QLatin1String("..");
}

// PBServer uuids (QUuid without braces) likewise become file names on disk.
bool validUuid(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^[0-9a-fA-F-]{8,64}$"));
    return re.match(s).hasMatch();
}

QHttpServerResponse jsonResp(const QJsonObject &o, StatusCode status = StatusCode::Ok)
{
    QHttpServerResponse resp(QByteArray("application/json"),
                             QJsonDocument(o).toJson(QJsonDocument::Compact), status);
    addSecurityHeaders(resp);
    return resp;
}

QHttpServerResponse jsonResp(const QJsonArray &a, StatusCode status = StatusCode::Ok)
{
    QHttpServerResponse resp(QByteArray("application/json"),
                             QJsonDocument(a).toJson(QJsonDocument::Compact), status);
    addSecurityHeaders(resp);
    return resp;
}

QHttpServerResponse errResp(const QString &msg, StatusCode status)
{
    QJsonObject o;
    o["error"] = msg;
    return jsonResp(o, status);
}

QHttpServerResponse unauthorized() { return errResp(QStringLiteral("authentication required"), StatusCode::Unauthorized); }
QHttpServerResponse forbidden()    { return errResp(QStringLiteral("forbidden"), StatusCode::Forbidden); }

QHttpServerResponse okResp()
{
    QJsonObject o;
    o["ok"] = true;
    return jsonResp(o);
}

// Confine a path to the configured paths/scripts directory (ported from the old
// IPC daemon's saveScriptConfined). Returns the canonical target or empty on reject.
QString confineToScripts(const QString &path)
{
    tiConfMain cfg;
    const QString base = QDir::cleanPath(QDir(cfg.getValue("paths/scripts").toString()).absolutePath());
    const QString target = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if(base.isEmpty() || (target != base && !target.startsWith(base + QLatin1Char('/'))))
        return QString();
    return target;
}

bool saveScriptConfined(const QString &path, const QString &content)
{
    const QString target = confineToScripts(path);
    if(target.isEmpty())
        return false;
    QFile f(target);
    if(!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream out(&f);
    out << content;
    f.close();
    return true;
}

QByteArray buildSessionCookie(const QString &token, bool secure, bool clear = false)
{
    QByteArray c = "tibackup_sid=";
    if(!clear)
        c += token.toUtf8();
    c += "; HttpOnly; SameSite=Strict; Path=/";
    if(secure)
        c += "; Secure";
    c += clear ? "; Max-Age=0" : "; Max-Age=3600";
    return c;
}

QHttpServerResponse jsonRespCookie(const QJsonObject &o, const QByteArray &setCookie,
                                   StatusCode status = StatusCode::Ok)
{
    QHttpServerResponse resp(QByteArray("application/json"),
                             QJsonDocument(o).toJson(QJsonDocument::Compact), status);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    QHttpHeaders h = resp.headers();
    h.append(QHttpHeaders::WellKnownHeader::SetCookie, setCookie);
    h.append("X-Content-Type-Options", "nosniff");
    resp.setHeaders(std::move(h));
#else
    resp.addHeader("Set-Cookie", setCookie);
    resp.addHeader("X-Content-Type-Options", "nosniff");
#endif
    return resp;
}

QJsonObject parseBody(const QHttpServerRequest &req)
{
    const QJsonDocument doc = QJsonDocument::fromJson(req.body());
    return doc.isObject() ? doc.object() : QJsonObject();
}

QString cookieValue(const QHttpServerRequest &req, const QByteArray &name)
{
    const QByteArray header = req.value("Cookie");
    const QList<QByteArray> parts = header.split(';');
    for(const QByteArray &p : parts)
    {
        const QByteArray t = p.trimmed();
        const int eq = t.indexOf('=');
        if(eq > 0 && t.left(eq) == name)
            return QString::fromUtf8(t.mid(eq + 1));
    }
    return QString();
}

QString statusToString(backupManager::backupStatus s)
{
    switch(s)
    {
    case backupManager::backupStatus::running:  return QStringLiteral("running");
    case backupManager::backupStatus::failed:   return QStringLiteral("failed");
    case backupManager::backupStatus::finished: return QStringLiteral("finished");
    case backupManager::backupStatus::standby:  return QStringLiteral("standby");
    }
    return QStringLiteral("standby");
}

} // namespace

ApiRouter::ApiRouter(QHttpServer *server, backupManager *manager, SessionStore *sessions)
    : m_server(server), m_manager(manager), m_sessions(sessions)
{
}

QString ApiRouter::sessionToken(const QHttpServerRequest &req) const
{
    return cookieValue(req, "tibackup_sid");
}

bool ApiRouter::isAuthed(const QHttpServerRequest &req) const
{
    return m_sessions->validate(sessionToken(req));
}

bool ApiRouter::csrfOk(const QHttpServerRequest &req) const
{
    const QString want = m_sessions->csrfFor(sessionToken(req));
    if(want.isEmpty())
        return false;
    return QString::fromUtf8(req.value("X-CSRF-Token")) == want;
}

void ApiRouter::registerRoutes()
{
    registerAuthRoutes();
    registerReadRoutes();
    registerWriteRoutes();
}

void ApiRouter::registerAuthRoutes()
{
    // GET /api/health (public) -------------------------------------------------
    m_server->route("/api/health", [this]() {
        QJsonObject o;
        o["status"]     = QStringLiteral("ok");
        o["version"]    = QString(tibackup_config::version);
        o["initSystem"] = (TiBackupLib::getSystemInitModel() == tiBackupInitSystem::Systemd)
                              ? QStringLiteral("systemd") : QStringLiteral("other");
        int running = 0;
        if(m_manager)
        {
            const QHash<QString, backupManager::backupStatus> *h = m_manager->getBackupStatus();
            for(auto it = h->cbegin(); it != h->cend(); ++it)
                if(it.value() == backupManager::backupStatus::running)
                    ++running;
        }
        o["runningJobs"] = running;
        return jsonResp(o);
    });

    // GET /api/auth/status (public) -------------------------------------------
    m_server->route("/api/auth/status", [this](const QHttpServerRequest &req) {
        tiConfMain cfg;
        QJsonObject o;
        o["setupRequired"] = cfg.getValue("web/passhash").toString().isEmpty();
        const bool authed = isAuthed(req);
        o["authenticated"] = authed;
        // Hand the session's CSRF token back to an already-authenticated caller so
        // a page reload - which clears the in-memory token but keeps the session
        // cookie - can keep performing writes instead of 403-ing until re-login.
        // Only disclosed to a request that already carries the valid (HttpOnly,
        // SameSite=Strict) session cookie, so CSRF protection is not weakened.
        if(authed)
            o["csrf"] = m_sessions->csrfFor(sessionToken(req));
        return jsonResp(o);
    });

    // POST /api/setup (public, one-shot) --------------------------------------
    m_server->route("/api/setup", Method::Post, [this](const QHttpServerRequest &req) {
        tiConfMain cfg;
        if(!cfg.getValue("web/passhash").toString().isEmpty())
            return errResp(QStringLiteral("setup already completed"), StatusCode::Forbidden);

        const QJsonObject body = parseBody(req);
        const QString pw = body["password"].toString();
        if(pw.size() < 8)
            return errResp(QStringLiteral("password must be at least 8 characters"), StatusCode::BadRequest);

        const QByteArray salt = passwordhash::generateSalt();
        cfg.setValue("web/salt", QString::fromLatin1(salt));
        cfg.setValue("web/passhash", passwordhash::hash(pw, salt));
        cfg.sync();

        QString csrf;
        const QString token = m_sessions->createSession(&csrf);
        QJsonObject o;
        o["ok"]   = true;
        o["csrf"] = csrf;
        return jsonRespCookie(o, buildSessionCookie(token, m_secureCookies));
    });

    // POST /api/auth/login (public) -------------------------------------------
    m_server->route("/api/auth/login", Method::Post, [this](const QHttpServerRequest &req) {
        const QString clientId = req.remoteAddress().toString();
        if(!m_sessions->loginAllowed(clientId))
            return errResp(QStringLiteral("too many failed attempts, try again later"),
                           StatusCode::TooManyRequests);

        tiConfMain cfg;
        const QString passhash = cfg.getValue("web/passhash").toString();
        const QByteArray salt  = cfg.getValue("web/salt").toString().toLatin1();
        if(passhash.isEmpty())
            return errResp(QStringLiteral("setup required"), StatusCode::Conflict);

        const QJsonObject body = parseBody(req);
        const QString pw = body["password"].toString();
        if(!passwordhash::verify(pw, salt, passhash))
        {
            m_sessions->loginFailed(clientId);
            return errResp(QStringLiteral("invalid credentials"), StatusCode::Unauthorized);
        }
        m_sessions->loginSucceeded(clientId);

        // We now hold a verified plaintext password: transparently migrate a
        // legacy (iterated-SHA-256) hash to the stronger PBKDF2 scheme.
        if(passwordhash::needsUpgrade(passhash))
        {
            const QByteArray newSalt = passwordhash::generateSalt();
            cfg.setValue("web/salt", QString::fromLatin1(newSalt));
            cfg.setValue("web/passhash", passwordhash::hash(pw, newSalt));
            cfg.sync();
        }

        QString csrf;
        const QString token = m_sessions->createSession(&csrf);
        QJsonObject o;
        o["ok"]   = true;
        o["csrf"] = csrf;
        return jsonRespCookie(o, buildSessionCookie(token, m_secureCookies));
    });

    // POST /api/auth/logout (auth + csrf) -------------------------------------
    m_server->route("/api/auth/logout", Method::Post, [this](const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        if(!csrfOk(req))   return forbidden();
        m_sessions->drop(sessionToken(req));
        QJsonObject o;
        o["ok"] = true;
        return jsonRespCookie(o, buildSessionCookie(QString(), m_secureCookies, true));
    });
}

void ApiRouter::registerReadRoutes()
{
    // GET /api/prefs ----------------------------------------------------------
    m_server->route("/api/prefs", Method::Get, [this](const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        tiConfMain cfg;
        QJsonObject o;
        o["debug"] = cfg.getValue("main/debug").toBool();

        QJsonObject paths;
        paths["backupjobs"] = cfg.getValue("paths/backupjobs").toString();
        paths["pbservers"]  = cfg.getValue("paths/pbservers").toString();
        paths["logs"]       = cfg.getValue("paths/logs").toString();
        paths["scripts"]    = cfg.getValue("paths/scripts").toString();
        o["paths"] = paths;

        QJsonObject smtp;
        smtp["server"]   = cfg.getValue("smtp/server").toString();
        smtp["auth"]     = cfg.getValue("smtp/auth").toBool();
        smtp["username"] = cfg.getValue("smtp/username").toString();
        smtp["password"] = QString::fromUtf8(
            QByteArray::fromBase64(cfg.getValue("smtp/password").toString().toLatin1()));
        o["smtp"] = smtp;

        return jsonResp(o);
    });

    // GET /api/jobs (list + live status) --------------------------------------
    m_server->route("/api/jobs", Method::Get, [this](const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        tiConfBackupJobs jobs;
        jobs.readBackupJobs();
        QJsonArray arr;
        for(tiBackupJob *job : jobs.getJobs())
        {
            QJsonObject o;
            o["name"]           = job->name;
            o["device"]         = job->device;
            o["partition_uuid"] = job->partition_uuid;
            o["status"]         = statusToString(m_manager->getBackupStatus(job->name));
            arr.append(o);
        }
        return jsonResp(arr);
    });

    // GET /api/jobs/<name> (full job) -----------------------------------------
    m_server->route("/api/jobs/<arg>", Method::Get,
        [this](const QString &name, const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        tiConfBackupJobs jobs;
        jobs.readBackupJobs();
        tiBackupJob *job = jobs.getJobByName(name);
        if(!job) return errResp(QStringLiteral("job not found"), StatusCode::NotFound);
        QJsonObject o = jsonmap::jobToJson(*job);
        o["status"] = statusToString(m_manager->getBackupStatus(name));
        return jsonResp(o);
    });

    // GET /api/devices --------------------------------------------------------
    m_server->route("/api/devices", [this](const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        TiBackupLib lib;
        QList<DeviceDisk> disks = lib.getAttachedDisks();
        QJsonArray arr;
        for(DeviceDisk &d : disks)
        {
            d.readPartitions();
            arr.append(jsonmap::diskToJson(d));
        }
        return jsonResp(arr);
    });

    // GET /api/devices/partition/<uuid> ---------------------------------------
    m_server->route("/api/devices/partition/<arg>", Method::Get,
        [this](const QString &uuid, const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        DeviceDiskPartition part = TiBackupLib::getPartitionByUUID(uuid);
        if(part.uuid.isEmpty())
            return errResp(QStringLiteral("partition not found"), StatusCode::NotFound);
        TiBackupLib lib;
        QJsonObject o = jsonmap::partitionToJson(part);
        o["mounted"]  = lib.isMounted(&part);
        o["mountDir"] = lib.getMountDir(&part);
        return jsonResp(o);
    });

    // GET /api/pbs (list, secrets omitted) ------------------------------------
    m_server->route("/api/pbs", Method::Get, [this](const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        tiConfPBServers::instance()->readItems();
        QJsonArray arr;
        for(PBServer *srv : tiConfPBServers::instance()->getItems())
            arr.append(jsonmap::pbServerToJson(*srv, false));
        return jsonResp(arr);
    });

    // GET /api/pbs/<uuid> -----------------------------------------------------
    m_server->route("/api/pbs/<arg>", Method::Get,
        [this](const QString &uuid, const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        tiConfPBServers::instance()->readItems();
        PBServer *srv = tiConfPBServers::instance()->getItemByUuid(uuid);
        if(!srv) return errResp(QStringLiteral("server not found"), StatusCode::NotFound);
        return jsonResp(jsonmap::pbServerToJson(*srv, false));
    });

    // GET /api/logs/main?lines=N ----------------------------------------------
    m_server->route("/api/logs/main", [this](const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        int lines = req.query().queryItemValue("lines").toInt();
        if(lines <= 0) lines = 200;

        tiConfMain cfg;
        const QString path = cfg.getValue("paths/logs").toString() + "/tibackup.log";
        QByteArray content;
        QFile f(path);
        if(f.open(QIODevice::ReadOnly))
        {
            if(f.size() > kMaxLogTailBytes)
                f.seek(f.size() - kMaxLogTailBytes);
            content = f.readAll();
            f.close();
        }
        const QList<QByteArray> all = content.split('\n');
        QStringList out;
        const int start = qMax(0, static_cast<int>(all.size()) - lines);
        for(int i = start; i < all.size(); ++i)
            out << QString::fromUtf8(all[i]);

        QJsonObject o;
        o["text"] = out.join('\n');
        return jsonResp(o);
    });

    // GET /api/logs/runs ------------------------------------------------------
    m_server->route("/api/logs/runs", [this](const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        tiConfMain cfg;
        QDir d(cfg.getLogsDetailDir());
        QJsonArray arr;
        const QFileInfoList list = d.entryInfoList(QStringList() << "*.log", QDir::Files, QDir::Name);
        for(const QFileInfo &fi : list)
        {
            const QString base = fi.completeBaseName();   // <date>__<name>
            const QStringList parts = base.split(QStringLiteral("__"));
            QJsonObject o;
            o["file"] = base;
            o["date"] = parts.value(0);
            o["name"] = parts.value(1);
            arr.append(o);
        }
        return jsonResp(arr);
    });

    // GET /api/logs/runs/<file> -----------------------------------------------
    m_server->route("/api/logs/runs/<arg>", Method::Get,
        [this](const QString &file, const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        if(file.contains('/') || file.contains(QStringLiteral("..")))
            return errResp(QStringLiteral("invalid file"), StatusCode::BadRequest);

        tiConfMain cfg;
        const QString full = cfg.getLogsDetailDir() + "/" + file + ".log";
        QFile f(full);
        if(!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return errResp(QStringLiteral("log not found"), StatusCode::NotFound);

        QJsonObject o;
        o["text"] = QString::fromUtf8(f.readAll());
        return jsonResp(o);
    });

    // GET /api/browse?root=&path=&uuid=&files= --------------------------------
    m_server->route("/api/browse", [this](const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        const QUrlQuery q(req.query());
        const QString root = q.queryItemValue("root");
        const QString path = q.queryItemValue("path", QUrl::FullyDecoded);
        const QString uuid = q.queryItemValue("uuid");
        const bool files   = q.queryItemValue("files") == QLatin1String("1");

        tiConfMain cfg;
        QString base;
        if(root == QLatin1String("script"))
        {
            base = cfg.getValue("paths/scripts").toString();
        }
        else if(root == QLatin1String("dest"))
        {
            if(uuid.isEmpty())
                return errResp(QStringLiteral("uuid required for dest browse"), StatusCode::BadRequest);
            DeviceDiskPartition part = TiBackupLib::getPartitionByUUID(uuid);
            TiBackupLib lib;
            if(!lib.isMounted(&part))
                return errResp(QStringLiteral("partition not mounted"), StatusCode::Conflict);
            base = lib.getMountDir(&part);
        }
        else
        {
            base = QStringLiteral("/");   // src / keyfile: full filesystem (authenticated admin)
        }

        if(base.isEmpty())
            return errResp(QStringLiteral("invalid browse root"), StatusCode::BadRequest);

        const QString canonicalBase = QDir(base).canonicalPath();
        if(canonicalBase.isEmpty())
            return errResp(QStringLiteral("browse root unavailable"), StatusCode::NotFound);

        QString canonicalTarget = QDir(path.isEmpty() ? base : path).canonicalPath();
        // When base is "/", "base + '/'" would be "//"; use "/" as the prefix so
        // navigation into subdirectories of root is allowed.
        const QString baseSep = (canonicalBase == QLatin1String("/"))
            ? canonicalBase : canonicalBase + QLatin1Char('/');
        const bool insideBase = !canonicalTarget.isEmpty() &&
            (canonicalTarget == canonicalBase || canonicalTarget.startsWith(baseSep));
        if(!insideBase)
            canonicalTarget = canonicalBase;   // clamp escapes back to the confinement root

        QDir::Filters filt = QDir::Dirs | QDir::NoDotAndDotDot;
        if(files)
            filt |= QDir::Files;

        QJsonArray entries;
        const QFileInfoList list = QDir(canonicalTarget).entryInfoList(filt, QDir::Name | QDir::DirsFirst);
        for(const QFileInfo &fi : list)
        {
            QJsonObject e;
            e["name"]  = fi.fileName();
            e["isDir"] = fi.isDir();
            e["size"]  = static_cast<qint64>(fi.isDir() ? 0 : fi.size());
            e["mtime"] = fi.lastModified().toSecsSinceEpoch();
            entries.append(e);
        }

        QString parent = (canonicalTarget == canonicalBase)
            ? canonicalBase : QFileInfo(canonicalTarget).absolutePath();
        if(!(parent == canonicalBase || parent.startsWith(baseSep)))
            parent = canonicalBase;

        QJsonObject o;
        o["base"]    = canonicalBase;
        o["path"]    = canonicalTarget;
        o["parent"]  = parent;
        o["entries"] = entries;
        return jsonResp(o);
    });

    // GET /api/scripts?path= (read confined script) ---------------------------
    m_server->route("/api/scripts", Method::Get, [this](const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        const QString path = req.query().queryItemValue("path", QUrl::FullyDecoded);
        const QString target = confineToScripts(path);
        if(target.isEmpty())
            return errResp(QStringLiteral("path outside scripts directory"), StatusCode::Forbidden);
        QFile f(target);
        if(!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return errResp(QStringLiteral("script not found"), StatusCode::NotFound);
        QJsonObject o;
        o["path"]    = target;
        o["content"] = QString::fromUtf8(f.readAll());
        return jsonResp(o);
    });
}

void ApiRouter::registerWriteRoutes()
{
    // PUT /api/prefs ----------------------------------------------------------
    m_server->route("/api/prefs", Method::Put, [this](const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        if(!csrfOk(req))   return forbidden();
        const QJsonObject b = parseBody(req);
        tiConfMain cfg;
        if(b.contains("debug"))
        {
            const bool dbg = b["debug"].toBool();
            cfg.setValue("main/debug", dbg);
            tibackup::setDebugLogging(dbg);   // take effect without a daemon restart
        }
        if(b.contains("paths"))
        {
            const QJsonObject p = b["paths"].toObject();
            for(const QString &k : {QStringLiteral("backupjobs"), QStringLiteral("pbservers"),
                                    QStringLiteral("logs"), QStringLiteral("scripts")})
                if(p.contains(k))
                    cfg.setValue("paths/" + k, p[k].toString());
        }
        if(b.contains("smtp"))
        {
            const QJsonObject s = b["smtp"].toObject();
            if(s.contains("server"))   cfg.setValue("smtp/server", s["server"].toString());
            if(s.contains("auth"))     cfg.setValue("smtp/auth", s["auth"].toBool());
            if(s.contains("username")) cfg.setValue("smtp/username", s["username"].toString());
            if(s.contains("password"))
                cfg.setValue("smtp/password",
                    QString::fromLatin1(s["password"].toString().toUtf8().toBase64()));
        }
        cfg.sync();
        return okResp();
    });

    // POST /api/jobs (create) -------------------------------------------------
    m_server->route("/api/jobs", Method::Post, [this](const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        if(!csrfOk(req))   return forbidden();
        const tiBackupJob job = jsonmap::jobFromJson(parseBody(req));
        if(!validIdentifier(job.name))
            return errResp(QStringLiteral("invalid job name (allowed: letters, digits, . _ -)"),
                           StatusCode::BadRequest);
        tiConfBackupJobs jobs;
        jobs.readBackupJobs();
        if(jobs.getJobByName(job.name))
            return errResp(QStringLiteral("a job with that name already exists"), StatusCode::Conflict);
        jobs.saveBackupJob(job);
        return okResp();
    });

    // PUT /api/jobs/<name> (update, rename if name changed) -------------------
    m_server->route("/api/jobs/<arg>", Method::Put,
        [this](const QString &name, const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        if(!csrfOk(req))   return forbidden();
        if(!validIdentifier(name))
            return errResp(QStringLiteral("invalid job name"), StatusCode::BadRequest);
        tiBackupJob job = jsonmap::jobFromJson(parseBody(req));
        tiConfBackupJobs jobs;
        jobs.readBackupJobs();
        if(!jobs.getJobByName(name))
            return errResp(QStringLiteral("job not found"), StatusCode::NotFound);
        if(!job.name.isEmpty() && job.name != name)
        {
            if(!validIdentifier(job.name))
                return errResp(QStringLiteral("invalid job name"), StatusCode::BadRequest);
            if(!jobs.renameJob(name, job.name))
                return errResp(QStringLiteral("rename failed (target exists?)"), StatusCode::Conflict);
        }
        else
        {
            job.name = name;
        }
        jobs.saveBackupJob(job);
        return okResp();
    });

    // DELETE /api/jobs/<name> -------------------------------------------------
    m_server->route("/api/jobs/<arg>", Method::Delete,
        [this](const QString &name, const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        if(!csrfOk(req))   return forbidden();
        if(!validIdentifier(name))
            return errResp(QStringLiteral("invalid job name"), StatusCode::BadRequest);
        tiConfBackupJobs jobs;
        if(!jobs.removeJobByName(name))
            return errResp(QStringLiteral("delete failed"), StatusCode::NotFound);
        return okResp();
    });

    // POST /api/jobs/<name>/start --------------------------------------------
    m_server->route("/api/jobs/<arg>/start", Method::Post,
        [this](const QString &name, const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        if(!csrfOk(req))   return forbidden();
        const bool started = m_manager->startBackup(name);
        QJsonObject o;
        o["ok"]      = true;
        o["started"] = started;
        return jsonResp(o);
    });

    // POST /api/pbs (create) --------------------------------------------------
    m_server->route("/api/pbs", Method::Post, [this](const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        if(!csrfOk(req))   return forbidden();
        PBServer srv = jsonmap::pbServerFromJson(parseBody(req));
        if(srv.name.isEmpty() || srv.host.isEmpty())
            return errResp(QStringLiteral("name and host required"), StatusCode::BadRequest);
        // The uuid becomes the on-disk file name; reject a client-supplied value
        // that isn't a plain uuid so it can't traverse out of the pbservers dir.
        if(!validUuid(srv.uuid))
            return errResp(QStringLiteral("invalid server id"), StatusCode::BadRequest);
        tiConfPBServers::instance()->saveItem(srv);
        QJsonObject o;
        o["ok"]   = true;
        o["uuid"] = srv.uuid;
        return jsonResp(o);
    });

    // PUT /api/pbs/<uuid> (update; secrets preserved if omitted) -------------
    m_server->route("/api/pbs/<arg>", Method::Put,
        [this](const QString &uuid, const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        if(!csrfOk(req))   return forbidden();
        if(!validUuid(uuid))
            return errResp(QStringLiteral("invalid server id"), StatusCode::BadRequest);
        tiConfPBServers::instance()->readItems();
        PBServer *existing = tiConfPBServers::instance()->getItemByUuid(uuid);
        if(!existing)
            return errResp(QStringLiteral("server not found"), StatusCode::NotFound);
        PBServer srv = jsonmap::pbServerFromJson(parseBody(req));
        srv.uuid = uuid;
        if(srv.password.isEmpty()) srv.password = existing->password;
        if(srv.keypass.isEmpty())  srv.keypass  = existing->keypass;
        tiConfPBServers::instance()->saveItem(srv);
        return okResp();
    });

    // DELETE /api/pbs/<uuid> --------------------------------------------------
    m_server->route("/api/pbs/<arg>", Method::Delete,
        [this](const QString &uuid, const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        if(!csrfOk(req))   return forbidden();
        if(!validUuid(uuid))
            return errResp(QStringLiteral("invalid server id"), StatusCode::BadRequest);
        if(!tiConfPBServers::instance()->removeItemByUuid(uuid))
            return errResp(QStringLiteral("delete failed"), StatusCode::NotFound);
        return okResp();
    });

    // POST /api/pbs/test ------------------------------------------------------
    m_server->route("/api/pbs/test", Method::Post, [this](const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        if(!csrfOk(req))   return forbidden();
        const QJsonObject b = parseBody(req);
        QString host, user, pass;
        int port = 8007;
        // Test an existing, saved server by uuid; otherwise test the inline
        // host/user/pass from the form. The "Add server" form always carries a
        // uuid key (empty string for a not-yet-saved server), so check that the
        // uuid is non-empty rather than merely present - an empty uuid means
        // "new server, use the inline fields", not "look up server ''".
        const QString uuid = b.value("uuid").toString();
        if(!uuid.isEmpty())
        {
            tiConfPBServers::instance()->readItems();
            PBServer *s = tiConfPBServers::instance()->getItemByUuid(uuid);
            if(!s) return errResp(QStringLiteral("server not found"), StatusCode::NotFound);
            host = s->host; port = s->port; user = s->username; pass = s->password;
        }
        else
        {
            host = b["host"].toString();
            port = b["port"].toInt(8007);
            user = b["username"].toString();
            pass = b["password"].toString();
        }
        pbsClient *c = pbsClient::instanceUnique();
        const HttpStatus::Code code = c->auth(host, port, user, pass);
        c->deleteLater();
        QJsonObject o;
        o["ok"]     = HttpStatus::isSuccessful(code);
        o["status"] = static_cast<int>(code);
        return jsonResp(o);
    });

    // GET /api/pbs/<uuid>/datastores -----------------------------------------
    m_server->route("/api/pbs/<arg>/datastores", Method::Get,
        [this](const QString &uuid, const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        tiConfPBServers::instance()->readItems();
        PBServer *s = tiConfPBServers::instance()->getItemByUuid(uuid);
        if(!s) return errResp(QStringLiteral("server not found"), StatusCode::NotFound);
        pbsClient *c = pbsClient::instanceUnique();
        if(!HttpStatus::isSuccessful(c->auth(s->host, s->port, s->username, s->password)))
        {
            c->deleteLater();
            return errResp(QStringLiteral("PBS authentication failed"), StatusCode::BadGateway);
        }
        const pbsClient::HttpResponse resp = c->getDatastores();
        c->deleteLater();
        if(!HttpStatus::isSuccessful(resp.status))
            return errResp(QStringLiteral("PBS request failed"), StatusCode::BadGateway);
        return jsonResp(resp.data.object().value("data").toArray());
    });

    // GET /api/pbs/<uuid>/datastores/<ds>/groups -----------------------------
    m_server->route("/api/pbs/<arg>/datastores/<arg>/groups", Method::Get,
        [this](const QString &uuid, const QString &datastore, const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        tiConfPBServers::instance()->readItems();
        PBServer *s = tiConfPBServers::instance()->getItemByUuid(uuid);
        if(!s) return errResp(QStringLiteral("server not found"), StatusCode::NotFound);
        pbsClient *c = pbsClient::instanceUnique();
        if(!HttpStatus::isSuccessful(c->auth(s->host, s->port, s->username, s->password)))
        {
            c->deleteLater();
            return errResp(QStringLiteral("PBS authentication failed"), StatusCode::BadGateway);
        }
        const pbsClient::HttpResponse resp = c->getDatastoreGroups(datastore);
        c->deleteLater();
        if(!HttpStatus::isSuccessful(resp.status))
            return errResp(QStringLiteral("PBS request failed"), StatusCode::BadGateway);
        return jsonResp(resp.data.object().value("data").toArray());
    });

    // PUT /api/scripts {path, content} ---------------------------------------
    m_server->route("/api/scripts", Method::Put, [this](const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        if(!csrfOk(req))   return forbidden();
        const QJsonObject b = parseBody(req);
        if(!saveScriptConfined(b["path"].toString(), b["content"].toString()))
            return errResp(QStringLiteral("path must be under the configured scripts directory"),
                           StatusCode::Forbidden);
        return okResp();
    });

    // POST /api/devices/partition/<uuid>/mount -------------------------------
    m_server->route("/api/devices/partition/<arg>/mount", Method::Post,
        [this](const QString &uuid, const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        if(!csrfOk(req))   return forbidden();
        DeviceDiskPartition part = TiBackupLib::getPartitionByUUID(uuid);
        if(part.uuid.isEmpty())
            return errResp(QStringLiteral("partition not found"), StatusCode::NotFound);
        const QJsonObject b = parseBody(req);
        tiBackupJob job;
        job.encLUKSType     = static_cast<tiBackupEncLUKS>(b["luksType"].toInt());
        job.encLUKSFilePath = b["luksFilePath"].toString();
        TiBackupLib lib;
        const QString dir = lib.mountPartition(&part, &job);
        if(dir.isEmpty())
            return errResp(QStringLiteral("mount failed"), StatusCode::InternalServerError);
        QJsonObject o;
        o["ok"]       = true;
        o["mountDir"] = dir;
        return jsonResp(o);
    });

    // POST /api/devices/partition/<uuid>/umount ------------------------------
    m_server->route("/api/devices/partition/<arg>/umount", Method::Post,
        [this](const QString &uuid, const QHttpServerRequest &req) {
        if(!isAuthed(req)) return unauthorized();
        if(!csrfOk(req))   return forbidden();
        DeviceDiskPartition part = TiBackupLib::getPartitionByUUID(uuid);
        TiBackupLib lib;
        lib.umountPartition(&part);
        return okResp();
    });
}
