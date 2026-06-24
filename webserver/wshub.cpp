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

#include "webserver/wshub.h"

#include <QHttpServer>
#include <QAbstractHttpServer>
#include <QHttpServerRequest>
#include <QWebSocket>
#include <QNetworkRequest>
#include <QFileSystemWatcher>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonDocument>

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <QHttpServerWebSocketUpgradeResponse>
#endif

#include "config.h"
#include "backupmanager.h"
#include "webserver/auth/sessionstore.h"

namespace {

// The upgrade verifier is a plain (captureless) callable, so it reaches the
// session store through this file-scope pointer set in the WsHub constructor.
SessionStore *g_wsSessions = nullptr;

QString wsCookieTokenFromHeader(const QByteArray &header)
{
    const QList<QByteArray> parts = header.split(';');
    for(const QByteArray &p : parts)
    {
        const QByteArray t = p.trimmed();
        const int eq = t.indexOf('=');
        if(eq > 0 && t.left(eq) == "tibackup_sid")
            return QString::fromUtf8(t.mid(eq + 1));
    }
    return QString();
}

[[maybe_unused]] QString wsCookieToken(const QHttpServerRequest &req)
{
    return wsCookieTokenFromHeader(req.value("Cookie"));
}

QString statusString(backupManager::backupStatus s)
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

WsHub::WsHub(QHttpServer *server, backupManager *manager, SessionStore *sessions,
             const QString &logPath, QObject *parent)
    : QObject(parent), m_server(server), m_manager(manager), m_sessions(sessions), m_logPath(logPath)
{
    g_wsSessions = sessions;

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // Authorise /ws upgrades by the session cookie. Without a verifier Qt >= 6.8
    // denies all upgrades, so this is required for the WebSocket to work at all.
    m_server->addWebSocketUpgradeVerifier(this,
        [](const QHttpServerRequest &req) -> QHttpServerWebSocketUpgradeResponse {
            if(req.url().path() != QLatin1String("/ws"))
                return QHttpServerWebSocketUpgradeResponse::passToNext();
            if(g_wsSessions && g_wsSessions->validate(wsCookieToken(req)))
                return QHttpServerWebSocketUpgradeResponse::accept();
            return QHttpServerWebSocketUpgradeResponse::deny();
        });
#endif

    connect(m_server, &QAbstractHttpServer::newWebSocketConnection,
            this, [this]() { acceptPending(); });

    if(m_manager)
    {
        connect(m_manager, &backupManager::statusChanged, this,
            [this](const QString &name, backupManager::backupStatus s) {
                QJsonObject o;
                o["type"]   = QStringLiteral("jobStatus");
                o["name"]   = name;
                o["status"] = statusString(s);
                broadcast(QJsonDocument(o).toJson(QJsonDocument::Compact));
            });
    }

    if(!m_logPath.isEmpty())
    {
        const QFileInfo fi(m_logPath);
        m_logPos = fi.exists() ? fi.size() : 0;

        m_watcher = new QFileSystemWatcher(this);
        m_watcher->addPath(fi.absolutePath());          // survive log (re)creation
        if(fi.exists())
            m_watcher->addPath(m_logPath);

        connect(m_watcher, &QFileSystemWatcher::fileChanged, this,
                [this](const QString &) { emitLogTail(); });
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
                [this](const QString &) {
                    if(!m_watcher->files().contains(m_logPath) && QFileInfo::exists(m_logPath))
                        m_watcher->addPath(m_logPath);
                    emitLogTail();
                });
    }
}

void WsHub::acceptPending()
{
    while(m_server->hasPendingWebSocketConnections())
    {
        std::unique_ptr<QWebSocket> sock = m_server->nextPendingWebSocketConnection();
        if(!sock)
            break;
        QWebSocket *ws = sock.release();
        ws->setParent(this);

#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
        // Qt < 6.8 has no addWebSocketUpgradeVerifier(), so the upgrade reaches us
        // unauthenticated. Authorise it here from the handshake session cookie;
        // otherwise any client that can reach the port could stream the live log
        // tail. (On >= 6.8 the upgrade verifier in the ctor already did this.)
        if(!m_sessions || !m_sessions->validate(
               wsCookieTokenFromHeader(ws->request().rawHeader("Cookie"))))
        {
            ws->close(QWebSocketProtocol::CloseCodePolicyViolated, QStringLiteral("unauthorized"));
            ws->deleteLater();
            continue;
        }
#endif

        m_clients.append(ws);

        connect(ws, &QWebSocket::disconnected, this, [this, ws]() {
            m_clients.removeAll(ws);
            ws->deleteLater();
        });

        QJsonObject hello;
        hello["type"]    = QStringLiteral("hello");
        hello["version"] = QString(tibackup_config::version);
        ws->sendTextMessage(QString::fromUtf8(QJsonDocument(hello).toJson(QJsonDocument::Compact)));
    }
}

void WsHub::emitLogTail()
{
    QFile f(m_logPath);
    if(!f.open(QIODevice::ReadOnly))
        return;

    const qint64 size = f.size();
    if(size < m_logPos)         // file was truncated/rotated
        m_logPos = 0;
    f.seek(m_logPos);
    const QByteArray chunk = f.readAll();
    m_logPos = f.pos();
    f.close();

    if(chunk.isEmpty())
        return;

    const QList<QByteArray> lines = chunk.split('\n');
    for(const QByteArray &line : lines)
    {
        if(line.trimmed().isEmpty())
            continue;
        QJsonObject o;
        o["type"] = QStringLiteral("logTail");
        o["line"] = QString::fromUtf8(line);
        broadcast(QJsonDocument(o).toJson(QJsonDocument::Compact));
    }
}

void WsHub::broadcast(const QByteArray &payload)
{
    const QString msg = QString::fromUtf8(payload);
    for(QWebSocket *ws : m_clients)
        ws->sendTextMessage(msg);
}
