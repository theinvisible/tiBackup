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

#include "webserver/auth/passwordhash.h"

#include <QCryptographicHash>
#include <QRandomGenerator>

QByteArray passwordhash::generateSalt(int bytes)
{
    QByteArray salt(bytes, Qt::Uninitialized);
    QRandomGenerator::system()->generate(salt.begin(), salt.end());
    return salt.toHex();
}

QString passwordhash::hash(const QString &password, const QByteArray &saltHex, int iterations)
{
    const QByteArray salt = QByteArray::fromHex(saltHex);

    QByteArray digest = QCryptographicHash::hash(salt + password.toUtf8(),
                                                 QCryptographicHash::Sha256);
    for(int i = 1; i < iterations; ++i)
        digest = QCryptographicHash::hash(salt + digest, QCryptographicHash::Sha256);

    return QString::fromLatin1(digest.toHex());
}

bool passwordhash::verify(const QString &password, const QByteArray &saltHex,
                          const QString &expectedHashHex, int iterations)
{
    if(saltHex.isEmpty() || expectedHashHex.isEmpty())
        return false;

    const QByteArray a = hash(password, saltHex, iterations).toLatin1();
    const QByteArray b = expectedHashHex.toLatin1();

    if(a.size() != b.size())
        return false;

    quint8 diff = 0;
    for(int i = 0; i < a.size(); ++i)
        diff |= static_cast<quint8>(a[i]) ^ static_cast<quint8>(b[i]);
    return diff == 0;
}
