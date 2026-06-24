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

#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <QObject>
#include <QString>
#include <memory>

class QHttpServer;
class QTcpServer;
class QHttpServerRequest;
class QHttpServerResponder;
class backupManager;
class SessionStore;
class ApiRouter;
class WsHub;

// Owns the embedded QHttpServer (REST /api/* + static SPA) that replaces the old
// QtWebApp status page. Runs in-process in the (root) daemon and therefore calls
// the config/lib/backupManager classes directly — no IPC. Default bind is
// 127.0.0.1; LAN + TLS is opt-in via the [web] group in /etc/tibackup/main.conf.
class WebServer : public QObject
{
    Q_OBJECT
public:
    explicit WebServer(backupManager *manager, QObject *parent = nullptr);
    ~WebServer() override;

    bool isListening() const { return m_listening; }

private:
    // Binds either a plain QTcpServer or (when tls cert+key are configured) a
    // QSslServer, then hands it to QHttpServer. Centralizes the QHttpServer
    // version differences (Qt 6.4 listen() vs >=6.5 bind()).
    bool bindServer(const QString &bindAddr, quint16 port,
                    const QString &tlsCert, const QString &tlsKey);

    // Registers the static-file fallback; the /api/* routes come from ApiRouter.
    void registerStaticHandler();

    // Serves a file from the docroot for any unmatched GET (SPA static assets).
    void serveStatic(const QHttpServerRequest &request, QHttpServerResponder &responder);

    QHttpServer   *m_http    = nullptr;
    QTcpServer    *m_tcp     = nullptr;   // owned by m_http after bind() (parented)
    backupManager *m_manager = nullptr;
    QString        m_docroot;
    bool           m_listening = false;
    bool           m_tls       = false;   // serving over HTTPS (enables HSTS header)

    std::unique_ptr<SessionStore> m_sessions;
    std::unique_ptr<ApiRouter>    m_api;
    std::unique_ptr<WsHub>        m_ws;
};

#endif // WEBSERVER_H
