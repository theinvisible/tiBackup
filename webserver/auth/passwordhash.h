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

// Salted password hashing for the web admin login. Uses PBKDF2-HMAC-SHA256
// (QPasswordDigestor) — a proper, slow KDF that resists GPU/ASIC offline
// cracking far better than a plain iterated hash. Pure-Qt, no extra crypto
// dependency. Stored in main.conf as web/salt (hex) + web/passhash, where
// passhash is the self-describing string "pbkdf2_sha256$<iterations>$<hex>".
namespace passwordhash {

// Work factor for newly created hashes (OWASP 2023 guidance for PBKDF2-SHA256).
constexpr int kDefaultIterations = 210000;
// Work factor of the superseded iterated-SHA-256 scheme; kept only so hashes
// created before the PBKDF2 migration still verify (and then get upgraded).
constexpr int kLegacyIterations = 100000;

// Returns `bytes` random bytes, hex-encoded.
QByteArray generateSalt(int bytes = 16);

// PBKDF2 hash in the format "pbkdf2_sha256$<iterations>$<hex>". `saltHex` is the
// hex salt persisted alongside in web/salt.
QString hash(const QString &password, const QByteArray &saltHex,
             int iterations = kDefaultIterations);

// Constant-time verify. Accepts both the new "pbkdf2_sha256$..." format and the
// legacy bare-hex iterated-SHA-256 format, so existing passwords keep working.
bool verify(const QString &password, const QByteArray &saltHex,
            const QString &expectedHash, int iterations = kDefaultIterations);

// True if `storedHash` is in the legacy format and should be re-hashed with
// hash() and persisted after a successful verify (transparent upgrade).
bool needsUpgrade(const QString &storedHash);

} // namespace passwordhash

#endif // PASSWORDHASH_H
