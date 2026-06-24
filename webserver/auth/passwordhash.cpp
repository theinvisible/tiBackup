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
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QStringList>

namespace {

constexpr char kPbkdf2Prefix[] = "pbkdf2_sha256$";
constexpr int  kKeyLen = 32;   // derived key length in bytes (256-bit)

// Length-independent, content constant-time comparison of two byte arrays.
bool constTimeEqual(const QByteArray &a, const QByteArray &b)
{
    if(a.size() != b.size())
        return false;
    quint8 diff = 0;
    for(int i = 0; i < a.size(); ++i)
        diff |= static_cast<quint8>(a[i]) ^ static_cast<quint8>(b[i]);
    return diff == 0;
}

// The superseded scheme: SHA256(salt||password), then iterate SHA256(salt||digest).
QByteArray legacyHashHex(const QString &password, const QByteArray &saltHex, int iterations)
{
    const QByteArray salt = QByteArray::fromHex(saltHex);
    QByteArray digest = QCryptographicHash::hash(salt + password.toUtf8(),
                                                 QCryptographicHash::Sha256);
    for(int i = 1; i < iterations; ++i)
        digest = QCryptographicHash::hash(salt + digest, QCryptographicHash::Sha256);
    return digest.toHex();
}

} // namespace

QByteArray passwordhash::generateSalt(int bytes)
{
    QByteArray salt(bytes, Qt::Uninitialized);
    QRandomGenerator::system()->generate(salt.begin(), salt.end());
    return salt.toHex();
}

QString passwordhash::hash(const QString &password, const QByteArray &saltHex, int iterations)
{
    const QByteArray salt = QByteArray::fromHex(saltHex);
    const QByteArray derived = QPasswordDigestor::deriveKeyPbkdf2(
        QCryptographicHash::Sha256, password.toUtf8(), salt, iterations, kKeyLen);

    return QString::fromLatin1(kPbkdf2Prefix) + QString::number(iterations)
           + QLatin1Char('$') + QString::fromLatin1(derived.toHex());
}

bool passwordhash::verify(const QString &password, const QByteArray &saltHex,
                          const QString &expectedHash, int iterations)
{
    Q_UNUSED(iterations);   // the cost is taken from the stored hash / legacy const
    if(saltHex.isEmpty() || expectedHash.isEmpty())
        return false;

    if(expectedHash.startsWith(QLatin1String(kPbkdf2Prefix)))
    {
        const QStringList parts = expectedHash.split(QLatin1Char('$'));
        if(parts.size() != 3)
            return false;
        bool ok = false;
        const int storedIter = parts.at(1).toInt(&ok);
        if(!ok || storedIter <= 0)
            return false;

        const QByteArray salt = QByteArray::fromHex(saltHex);
        const QByteArray derived = QPasswordDigestor::deriveKeyPbkdf2(
            QCryptographicHash::Sha256, password.toUtf8(), salt, storedIter, kKeyLen);
        return constTimeEqual(derived.toHex(), parts.at(2).toLatin1());
    }

    // Legacy bare-hex iterated-SHA-256 hash (pre-migration installs).
    return constTimeEqual(legacyHashHex(password, saltHex, kLegacyIterations),
                          expectedHash.toLatin1());
}

bool passwordhash::needsUpgrade(const QString &storedHash)
{
    return !storedHash.startsWith(QLatin1String(kPbkdf2Prefix));
}
