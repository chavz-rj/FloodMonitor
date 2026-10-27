#include "LocalApiServer.h"

#include "FloodService.h"
#include "LocationHelper.h"

#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QPointer>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>

namespace {
const int kMaxRequestBytes = 300 * 1024;       // header + body hard limit
const int kMaxStoredValueChars = 200 * 1000;
const char *const kStorageKeys[] = {"settings", "cities", "weatherCache", "operator"};   // allow-list

bool isAllowedKey(const QString &key) {
    for (const char *k : kStorageKeys)
        if (key == QLatin1String(k))
            return true;
    return false;
}

QByteArray contentTypeFor(const QString &path) {
    if (path.endsWith(".html")) return "text/html; charset=utf-8";
    if (path.endsWith(".css"))  return "text/css; charset=utf-8";
    if (path.endsWith(".js"))   return "application/javascript; charset=utf-8";
    if (path.endsWith(".svg"))  return "image/svg+xml";
    if (path.endsWith(".png"))  return "image/png";
    return "application/octet-stream";
}
}

LocalApiServer::LocalApiServer(FloodService *service, const QString &dataDir, QObject *parent)
    : QObject(parent), m_server(new QTcpServer(this)), m_service(service),
    m_location(new LocationHelper(this)), m_dataDir(dataDir),
    m_token(QUuid::createUuid().toByteArray(QUuid::WithoutBraces)) {}

bool LocalApiServer::start() {
    connect(m_server, &QTcpServer::newConnection, this, &LocalApiServer::onNewConnection);
    return m_server->listen(QHostAddress::LocalHost, 0);   // loopback only, free port chosen by the OS
}

QUrl LocalApiServer::url() const {
    return QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(m_server->serverPort()));
}

void LocalApiServer::onNewConnection() {
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QTimer::singleShot(30000, socket, [socket]() { socket->abort(); });   // drop stalled clients
    }
}

void LocalApiServer::onReadyRead(QTcpSocket *socket) {
    const QByteArray data = socket->property("buf").toByteArray() + socket->readAll();
    if (data.size() > kMaxRequestBytes) {
        respond(socket, 413, "text/plain", "Request too large");
        return;
    }
    const int headerEnd = data.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        socket->setProperty("buf", data);
        return;
    }

    const QList<QByteArray> lines = data.left(headerEnd).split('\n');
    const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
    if (requestLine.size() < 2) {
        respond(socket, 400, "text/plain", "Bad request");
        return;
    }
    QHash<QByteArray, QByteArray> headers;
    for (int i = 1; i < lines.size(); i++) {
        const int colon = lines[i].indexOf(':');
        if (colon > 0)
            headers.insert(lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
    }

    const int contentLength = headers.value("content-length", "0").toInt();
    if (contentLength < 0 || contentLength > kMaxRequestBytes) {
        respond(socket, 413, "text/plain", "Request too large");
        return;
    }
    const QByteArray body = data.mid(headerEnd + 4);
    if (body.size() < contentLength) {   // body not complete yet
        socket->setProperty("buf", data);
        return;
    }
    socket->setProperty("buf", QByteArray());
    dispatch(socket, requestLine[0], QString::fromLatin1(requestLine[1]), headers, body.left(contentLength));
}

void LocalApiServer::dispatch(QTcpSocket *socket, const QByteArray &method, const QString &path,
                              const QHash<QByteArray, QByteArray> &headers, const QByteArray &body) {
    // Defence against DNS-rebinding: the Host header must be exactly our loopback address and port.
    const QByteArray expectedHost = QStringLiteral("127.0.0.1:%1").arg(m_server->serverPort()).toLatin1();
    if (headers.value("host") != expectedHost) {
        respond(socket, 403, "text/plain", "Forbidden");
        return;
    }

    if (method == "GET") {
        serveStatic(socket, path.section('?', 0, 0));
        return;
    }

    if (method == "POST" && path.startsWith(QLatin1String("/api/"))) {
        if (headers.value("x-app-token") != m_token) {
            respond(socket, 403, "text/plain", "Forbidden");
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument doc = body.isEmpty() ? QJsonDocument(QJsonObject()) : QJsonDocument::fromJson(body, &parseError);
        if (!doc.isObject()) {
            QJsonObject e;
            e["error"] = QStringLiteral("Request body must be a JSON object.");
            respondJson(socket, e, 400);
            return;
        }
        handleApi(socket, path.mid(5), doc.object());
        return;
    }
    respond(socket, 404, "text/plain", "Not found");
}

void LocalApiServer::serveStatic(QTcpSocket *socket, QString path) {
    if (path == QLatin1String("/"))
        path = QStringLiteral("/index.html");
    if (path.contains(QLatin1String("..")) || !path.startsWith('/')) {
        respond(socket, 404, "text/plain", "Not found");
        return;
    }
    QFile file(QStringLiteral(":/frontend") + path);   // only files compiled into the app can be served
    if (!file.open(QIODevice::ReadOnly)) {
        respond(socket, 404, "text/plain", "Not found");
        return;
    }
    QByteArray content = file.readAll();
    if (path == QLatin1String("/index.html")) {
        content.replace("__APP_TOKEN__", m_token);   // token is only ever delivered to the page we serve
        // adds the "Save files to phone" button without having to edit index.html
        content.replace("</body>", "<script src=\"js/export.js\"></script></body>");
    }
    respond(socket, 200, contentTypeFor(path), content);
}

void LocalApiServer::handleApi(QTcpSocket *socket, const QString &name, const QJsonObject &args) {
    // Allow-list: only these named calls exist. Nothing here can run arbitrary code or read arbitrary files.
    if (name == QLatin1String("startup"))        return respondJson(socket, m_service->startup());
    if (name == QLatin1String("board"))          return respondJson(socket, m_service->board());
    if (name == QLatin1String("history"))        return respondJson(socket, m_service->history());
    if (name == QLatin1String("statistics"))     return respondJson(socket, m_service->statistics());
    if (name == QLatin1String("report"))         return respondJson(socket, m_service->report());
    if (name == QLatin1String("exportFiles"))    return respondJson(socket, m_service->exportFiles());
    if (name == QLatin1String("setOperator"))    return respondJson(socket, m_service->setOperator(args.value("name").toString()));
    if (name == QLatin1String("updateLevel"))
        return respondJson(socket, m_service->updateLevel(args.value("segmentId").toInt(-1),
                                                          args.value("depth").toVariant().toString(),
                                                          args.value("confirmed").toBool(false)));
    if (name == QLatin1String("storageGet"))     return respondJson(socket, storageGet(args.value("key").toString()));
    if (name == QLatin1String("storageSet"))     return respondJson(socket, storageSet(args.value("key").toString(), args.value("value").toString()));
    if (name == QLatin1String("locate")) {
        QPointer<QTcpSocket> guard(socket);   // the socket may be gone by the time the GPS answers
        m_location->locate([this, guard](const QJsonObject &result) {
            if (guard)
                respondJson(guard, result);
        });
        return;
    }
    QJsonObject e;
    e["error"] = QStringLiteral("Unknown call");
    respondJson(socket, e, 404);
}

QJsonObject LocalApiServer::storageGet(const QString &key) const {
    QJsonObject o;
    if (!isAllowedKey(key)) { o["ok"] = false; return o; }
    QFile f(QDir(m_dataDir).filePath("kv_" + key + ".json"));
    o["ok"] = true;
    o["value"] = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    return o;
}

QJsonObject LocalApiServer::storageSet(const QString &key, const QString &value) const {
    QJsonObject o;
    if (!isAllowedKey(key) || value.size() > kMaxStoredValueChars) { o["ok"] = false; return o; }
    QSaveFile f(QDir(m_dataDir).filePath("kv_" + key + ".json"));   // atomic write: no half-written files
    o["ok"] = f.open(QIODevice::WriteOnly) && f.write(value.toUtf8()) >= 0 && f.commit();
    return o;
}

void LocalApiServer::respondJson(QTcpSocket *socket, const QJsonObject &object, int status) {
    respond(socket, status, "application/json; charset=utf-8", QJsonDocument(object).toJson(QJsonDocument::Compact));
}

void LocalApiServer::respond(QTcpSocket *socket, int status, const QByteArray &contentType, const QByteArray &body) {
    static const QHash<int, QByteArray> reasons = {{200, "OK"}, {400, "Bad Request"}, {403, "Forbidden"},
                                                   {404, "Not Found"}, {413, "Payload Too Large"}};
    QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + " " + reasons.value(status, "Error") + "\r\n";
    out += "Content-Type: " + contentType + "\r\n";
    out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    out += "Cache-Control: no-store\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n\r\n";
    socket->write(out + body);
    socket->disconnectFromHost();
}