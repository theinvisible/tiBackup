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

#include "webserver/webserver.h"

#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QHttpServerResponse>
#include <QTcpServer>
#include <QSslServer>
#include <QSslConfiguration>
#include <QSslCertificate>
#include <QSslKey>
#include <QHostAddress>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QJsonObject>
#include <QJsonDocument>

#include "config.h"
#include "ticonf.h"
#include "backupmanager.h"
#include "webserver/apirouter.h"
#include "webserver/auth/sessionstore.h"
#include "webserver/wshub.h"

namespace {

QByteArray contentTypeFor(const QString &path)
{
    const QString p = path.toLower();
    if(p.endsWith(".html") || p.endsWith(".htm")) return "text/html; charset=utf-8";
    if(p.endsWith(".js") || p.endsWith(".mjs"))   return "text/javascript; charset=utf-8";
    if(p.endsWith(".css"))   return "text/css; charset=utf-8";
    if(p.endsWith(".json"))  return "application/json";
    if(p.endsWith(".svg"))   return "image/svg+xml";
    if(p.endsWith(".png"))   return "image/png";
    if(p.endsWith(".jpg") || p.endsWith(".jpeg")) return "image/jpeg";
    if(p.endsWith(".webp"))  return "image/webp";
    if(p.endsWith(".ico"))   return "image/x-icon";
    if(p.endsWith(".woff2")) return "font/woff2";
    if(p.endsWith(".woff"))  return "font/woff";
    if(p.endsWith(".ttf"))   return "font/ttf";
    if(p.endsWith(".map"))   return "application/json";
    if(p.endsWith(".txt"))   return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

} // namespace

WebServer::WebServer(backupManager *manager, QObject *parent)
    : QObject(parent), m_manager(manager)
{
    tiConfMain cfg;

    m_docroot = cfg.getValue("web/docroot").toString();
    if(m_docroot.isEmpty())
        m_docroot = QStringLiteral("/var/lib/tibackup/www");

    QString bindAddr = cfg.getValue("web/bind").toString();
    if(bindAddr.isEmpty())
        bindAddr = QStringLiteral("127.0.0.1");

    bool ok = false;
    quint16 port = static_cast<quint16>(cfg.getValue("web/port").toUInt(&ok) & 0xFFFF);
    if(!ok || port == 0)
        port = 8877;

    // Dev/CI test overrides (only honoured when the env var is set), so the web UI
    // can be exercised without editing the root-owned /etc/tibackup/main.conf.
    if(qEnvironmentVariableIsSet("TIBACKUP_WEB_DOCROOT"))
        m_docroot = qEnvironmentVariable("TIBACKUP_WEB_DOCROOT");
    if(qEnvironmentVariableIsSet("TIBACKUP_WEB_PORT"))
        port = static_cast<quint16>(qEnvironmentVariable("TIBACKUP_WEB_PORT").toUInt() & 0xFFFF);

    const QString tlsCert = cfg.getValue("web/tls_cert").toString();
    const QString tlsKey  = cfg.getValue("web/tls_key").toString();
    const bool tls = !tlsCert.isEmpty() && !tlsKey.isEmpty();

    bool ttlOk = false;
    int ttl = cfg.getValue("web/session_ttl").toInt(&ttlOk);
    if(!ttlOk || ttl <= 0)
        ttl = 3600;

    m_http     = new QHttpServer(this);
    m_sessions = std::make_unique<SessionStore>(ttl);
    m_api      = std::make_unique<ApiRouter>(m_http, m_manager, m_sessions.get());
    m_api->setSecureCookies(tls);
    m_api->registerRoutes();

    registerStaticHandler();

    m_listening = bindServer(bindAddr, port, tlsCert, tlsKey);

    if(m_listening)
    {
        // Live push (job status + log tail) over /ws.
        const QString logPath = cfg.getValue("paths/logs").toString() + "/tibackup.log";
        m_ws = std::make_unique<WsHub>(m_http, m_manager, m_sessions.get(), logPath);

        qInfo("tiBackup web UI listening on %s://%s:%u (docroot %s)",
              tls ? "https" : "http", qPrintable(bindAddr), port, qPrintable(m_docroot));
    }
    else
        qWarning("tiBackup web UI failed to bind %s:%u", qPrintable(bindAddr), port);
}

WebServer::~WebServer() = default;

bool WebServer::bindServer(const QString &bindAddr, quint16 port,
                           const QString &tlsCert, const QString &tlsKey)
{
    QHostAddress addr(bindAddr);
    if(addr.isNull())
    {
        qWarning("tiBackup web UI: invalid bind address '%s', using 127.0.0.1", qPrintable(bindAddr));
        addr = QHostAddress(QStringLiteral("127.0.0.1"));
    }

    const bool tls = !tlsCert.isEmpty() && !tlsKey.isEmpty();

    if(tls)
    {
        QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();

        const QList<QSslCertificate> certs = QSslCertificate::fromPath(tlsCert);
        if(!certs.isEmpty())
            ssl.setLocalCertificate(certs.first());

        QFile keyFile(tlsKey);
        if(keyFile.open(QIODevice::ReadOnly))
        {
            QSslKey key(&keyFile, QSsl::Rsa, QSsl::Pem);
            if(key.isNull())
            {
                keyFile.seek(0);
                key = QSslKey(&keyFile, QSsl::Ec, QSsl::Pem);
            }
            keyFile.close();
            ssl.setPrivateKey(key);
        }

        auto *sslServer = new QSslServer(this);
        sslServer->setSslConfiguration(ssl);
        if(!sslServer->listen(addr, port))
        {
            qWarning("tiBackup web UI: TLS listen failed: %s", qPrintable(sslServer->errorString()));
            sslServer->deleteLater();
            return false;
        }
        m_tcp = sslServer;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        return m_http->bind(sslServer);
#else
        qWarning("tiBackup web UI: TLS requires Qt >= 6.5");
        return false;
#endif
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    auto *tcp = new QTcpServer(this);
    if(!tcp->listen(addr, port))
    {
        qWarning("tiBackup web UI: listen failed: %s", qPrintable(tcp->errorString()));
        tcp->deleteLater();
        return false;
    }
    m_tcp = tcp;
    return m_http->bind(tcp);
#else
    // Qt 6.4: QHttpServer::listen() binds internally.
    return m_http->listen(addr, port) != 0;
#endif
}

void WebServer::registerStaticHandler()
{
    // Static SPA assets for everything not handled by an /api route.
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    m_http->setMissingHandler(this,
        [this](const QHttpServerRequest &req, QHttpServerResponder &responder) {
            serveStatic(req, responder);
        });
#else
    m_http->setMissingHandler(
        [this](const QHttpServerRequest &req, QHttpServerResponder &&responder) {
            serveStatic(req, responder);
        });
#endif
}

void WebServer::serveStatic(const QHttpServerRequest &request, QHttpServerResponder &responder)
{
    QString path = request.url().path();

    // Unmatched /api/* must never fall through to a file; answer JSON 404.
    if(path.startsWith(QLatin1String("/api/")))
    {
        responder.sendResponse(QHttpServerResponse(QByteArray("application/json"),
            QByteArray("{\"error\":\"not found\"}"), QHttpServerResponse::StatusCode::NotFound));
        return;
    }

    if(path.isEmpty() || path == QLatin1String("/"))
        path = QStringLiteral("/index.html");

    // Collapse any ".." and build the absolute target under the docroot.
    const QString rel = QDir::cleanPath(path);             // begins with '/'
    const QString full = m_docroot + rel;

    const QString canonicalDoc  = QDir(m_docroot).canonicalPath();
    const QFileInfo fi(full);
    const QString canonicalFull = fi.canonicalFilePath();

    // Reject traversal / missing files; require the resolved path to stay inside docroot.
    if(canonicalDoc.isEmpty() || canonicalFull.isEmpty() ||
       !(canonicalFull == canonicalDoc || canonicalFull.startsWith(canonicalDoc + QLatin1Char('/'))) ||
       !fi.isFile())
    {
        responder.sendResponse(QHttpServerResponse(QHttpServerResponse::StatusCode::NotFound));
        return;
    }

    QFile f(canonicalFull);
    if(!f.open(QIODevice::ReadOnly))
    {
        responder.sendResponse(QHttpServerResponse(QHttpServerResponse::StatusCode::NotFound));
        return;
    }

    responder.sendResponse(QHttpServerResponse(contentTypeFor(canonicalFull), f.readAll()));
}
