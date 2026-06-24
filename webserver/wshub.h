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

#ifndef WSHUB_H
#define WSHUB_H

#include <QObject>
#include <QList>
#include <QString>

class QHttpServer;
class QWebSocket;
class QFileSystemWatcher;
class backupManager;
class SessionStore;

// Pushes live updates to connected browsers over a WebSocket at /ws:
//   {"type":"hello","version":...}
//   {"type":"jobStatus","name":...,"status":"running|finished|failed|standby"}
//   {"type":"logTail","line":"..."}
// The upgrade is authorised by the session cookie: via the upgrade verifier on
// Qt >= 6.8, and by re-checking the handshake cookie in acceptPending() on older
// Qt (which has no verifier API). An unauthenticated client is rejected on both.
class WsHub : public QObject
{
    Q_OBJECT
public:
    WsHub(QHttpServer *server, backupManager *manager, SessionStore *sessions,
          const QString &logPath, QObject *parent = nullptr);

private:
    void acceptPending();
    void emitLogTail();
    void broadcast(const QByteArray &payload);

    QHttpServer        *m_server;
    backupManager      *m_manager;
    SessionStore       *m_sessions;
    QList<QWebSocket *> m_clients;
    QFileSystemWatcher *m_watcher = nullptr;
    QString             m_logPath;
    qint64              m_logPos = 0;
};

#endif // WSHUB_H
