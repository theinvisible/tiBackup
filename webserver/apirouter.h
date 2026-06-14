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

#ifndef APIROUTER_H
#define APIROUTER_H

#include <QString>

class QHttpServer;
class QHttpServerRequest;
class backupManager;
class SessionStore;

// Registers all /api/* routes on the QHttpServer and maps them to the in-process
// lib/config/backupManager calls. Read routes live here (Phase 2); write routes
// and the manual-backup trigger are added in Phase 3. Every route except the
// public auth endpoints is gated by a valid session cookie; unsafe methods also
// require a matching CSRF header.
class ApiRouter
{
public:
    ApiRouter(QHttpServer *server, backupManager *manager, SessionStore *sessions);

    void setSecureCookies(bool secure) { m_secureCookies = secure; }
    void registerRoutes();

private:
    // --- auth helpers --------------------------------------------------------
    QString sessionToken(const QHttpServerRequest &req) const;   // from the cookie
    bool    isAuthed(const QHttpServerRequest &req) const;       // valid session
    bool    csrfOk(const QHttpServerRequest &req) const;         // matching CSRF header

    void registerAuthRoutes();
    void registerReadRoutes();
    void registerWriteRoutes();

    QHttpServer   *m_server;
    backupManager *m_manager;
    SessionStore  *m_sessions;
    bool           m_secureCookies = false;
};

#endif // APIROUTER_H
