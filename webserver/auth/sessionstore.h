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

#ifndef SESSIONSTORE_H
#define SESSIONSTORE_H

#include <QHash>
#include <QMutex>
#include <QString>

// In-memory session store for the web admin panel. Tokens are random and live
// only for the daemon's lifetime (restart == logged out), which is fine for a
// single-admin panel. Each session carries its own CSRF token. Thread-safe so
// the HTTP handlers and the WebSocket upgrade can both consult it.
class SessionStore
{
public:
    explicit SessionStore(int ttlSeconds = 3600);

    // Creates a session, returns its opaque token; the per-session CSRF token is
    // written to *csrfOut when provided.
    QString createSession(QString *csrfOut = nullptr);

    // True if the token exists and is not expired; refreshes its lastSeen.
    bool validate(const QString &token);

    // The CSRF token bound to a session, or empty if unknown/expired.
    QString csrfFor(const QString &token);

    void drop(const QString &token);

    // --- login throttling (brute-force mitigation) ---------------------------
    // Call before verifying credentials; returns false while `clientId` (the
    // remote IP) is locked out after too many recent failures.
    bool loginAllowed(const QString &clientId);
    // Record the outcome of a credential check so the lockout window can advance.
    void loginFailed(const QString &clientId);
    void loginSucceeded(const QString &clientId);

private:
    struct Session {
        QString csrf;
        qint64  lastSeen;   // epoch seconds
    };

    struct LoginGate {
        int    fails = 0;
        qint64 lockedUntil = 0;   // epoch seconds; 0 = not currently locked
    };

    void purgeExpired();    // caller holds m_mutex
    static QString randomToken(int bytes = 32);

    QHash<QString, Session>   m_sessions;
    QHash<QString, LoginGate> m_logins;
    int        m_ttl;
    QMutex     m_mutex;
};

#endif // SESSIONSTORE_H
