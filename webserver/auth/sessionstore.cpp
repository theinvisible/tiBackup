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

#include "webserver/auth/sessionstore.h"

#include <QDateTime>
#include <QRandomGenerator>

SessionStore::SessionStore(int ttlSeconds)
    : m_ttl(ttlSeconds)
{
}

QString SessionStore::randomToken(int bytes)
{
    QByteArray raw(bytes, Qt::Uninitialized);
    QRandomGenerator::system()->generate(raw.begin(), raw.end());
    return QString::fromLatin1(raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString SessionStore::createSession(QString *csrfOut)
{
    QMutexLocker lock(&m_mutex);
    purgeExpired();

    const QString token = randomToken();
    Session s;
    s.csrf     = randomToken();
    s.lastSeen = QDateTime::currentSecsSinceEpoch();
    m_sessions.insert(token, s);

    if(csrfOut)
        *csrfOut = s.csrf;
    return token;
}

bool SessionStore::validate(const QString &token)
{
    if(token.isEmpty())
        return false;

    QMutexLocker lock(&m_mutex);
    auto it = m_sessions.find(token);
    if(it == m_sessions.end())
        return false;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if(now - it->lastSeen > m_ttl)
    {
        m_sessions.erase(it);
        return false;
    }

    it->lastSeen = now;     // sliding expiry
    return true;
}

QString SessionStore::csrfFor(const QString &token)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_sessions.find(token);
    if(it == m_sessions.end())
        return QString();
    return it->csrf;
}

void SessionStore::drop(const QString &token)
{
    QMutexLocker lock(&m_mutex);
    m_sessions.remove(token);
}

void SessionStore::purgeExpired()
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for(auto it = m_sessions.begin(); it != m_sessions.end();)
    {
        if(now - it->lastSeen > m_ttl)
            it = m_sessions.erase(it);
        else
            ++it;
    }
}
