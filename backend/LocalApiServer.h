// LocalApiServer - tiny HTTP server bound to 127.0.0.1 only.
//  * GET  /...          serves the HTML/CSS/JS from Qt resources (":/frontend/...")
//  * POST /api/<name>   JSON calls into the C++ backend; every call must carry the per-launch token
// Why not QWebChannel: Qt WebView (the only web component available on Android) has no QWebChannel support.
#pragma once
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>

class QTcpServer;
class QTcpSocket;
class FloodService;
class LocationHelper;

class LocalApiServer : public QObject {
    Q_OBJECT
public:
    LocalApiServer(FloodService *service, const QString &dataDir, QObject *parent = nullptr);
    bool start();
    QUrl url() const;

private:
    void onNewConnection();
    void onReadyRead(QTcpSocket *socket);
    void dispatch(QTcpSocket *socket, const QByteArray &method, const QString &path,
                  const QHash<QByteArray, QByteArray> &headers, const QByteArray &body);
    void serveStatic(QTcpSocket *socket, QString path);
    void handleApi(QTcpSocket *socket, const QString &name, const QJsonObject &args);
    QJsonObject storageGet(const QString &key) const;
    QJsonObject storageSet(const QString &key, const QString &value) const;
    void respond(QTcpSocket *socket, int status, const QByteArray &contentType, const QByteArray &body);
    void respondJson(QTcpSocket *socket, const QJsonObject &object, int status = 200);

    QTcpServer *m_server;
    FloodService *m_service;
    LocationHelper *m_location;
    QString m_dataDir;
    QByteArray m_token;
};
