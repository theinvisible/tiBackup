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

#ifndef PASSWORDHASH_H
#define PASSWORDHASH_H

#include <QByteArray>
#include <QString>

// Salted, iterated SHA-256 password hashing for the web admin login. Pure-Qt
// (QCryptographicHash) so no extra crypto dependency. Stored in main.conf as
// web/salt (hex) + web/passhash (hex).
namespace passwordhash {

constexpr int kDefaultIterations = 100000;

// Returns `bytes` random bytes, hex-encoded.
QByteArray generateSalt(int bytes = 16);

// Hex-encoded SHA-256 digest of (salt || password), iterated.
QString hash(const QString &password, const QByteArray &saltHex,
             int iterations = kDefaultIterations);

// Constant-time comparison against a previously stored hex hash.
bool verify(const QString &password, const QByteArray &saltHex,
            const QString &expectedHashHex, int iterations = kDefaultIterations);

} // namespace passwordhash

#endif // PASSWORDHASH_H
