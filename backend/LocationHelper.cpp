#include "LocationHelper.h"

#include <QCoreApplication>
#include <QGeoPositionInfo>
#include <QGeoPositionInfoSource>
#include <QPermissions>
#include <QTimer>

LocationHelper::LocationHelper(QObject *parent) : QObject(parent) {}

QJsonObject LocationHelper::error(const QString &code, const QString &message) {
    QJsonObject o;
    o["ok"] = false;
    o["code"] = code;
    o["message"] = message;
    return o;
}

void LocationHelper::locate(Callback callback) {
    if (m_busy) {
        callback(error("busy", QStringLiteral("A location request is already running.")));
        return;
    }

    QLocationPermission permission;
    permission.setAccuracy(QLocationPermission::Approximate);   // city-level is enough for weather
    permission.setAvailability(QLocationPermission::WhenInUse);

    switch (qApp->checkPermission(permission)) {
    case Qt::PermissionStatus::Granted:
        startRequest(callback);
        break;
    case Qt::PermissionStatus::Denied:
        callback(error("denied", QStringLiteral("Location permission is turned off. Search for a city instead, or allow location in the phone's app settings.")));
        break;
    case Qt::PermissionStatus::Undetermined:
        m_busy = true;   // blocks a second prompt while the first is open
        qApp->requestPermission(permission, this, [this, callback](const QPermission &result) {
            m_busy = false;
            if (result.status() == Qt::PermissionStatus::Granted)
                startRequest(callback);
            else
                callback(error("denied", QStringLiteral("Location permission was not granted. You can still search for a city.")));
        });
        break;
    }
}

void LocationHelper::startRequest(Callback callback) {
    if (!m_source)
        m_source = QGeoPositionInfoSource::createDefaultSource(this);
    if (!m_source) {
        callback(error("unavailable", QStringLiteral("This device has no location provider. Search for a city instead.")));
        return;
    }

    m_busy = true;
    auto *context = new QObject(this);   // owns this request's connections; deleting it disconnects them all
    auto finished = std::make_shared<bool>(false);
    auto finish = [this, callback, context, finished](const QJsonObject &result) {
        if (*finished)
            return;
        *finished = true;
        m_busy = false;
        context->deleteLater();
        callback(result);
    };

    connect(m_source, &QGeoPositionInfoSource::positionUpdated, context, [finish](const QGeoPositionInfo &info) {
        QJsonObject o;
        o["ok"] = true;
        o["lat"] = info.coordinate().latitude();
        o["lon"] = info.coordinate().longitude();
        finish(o);
    });
    connect(m_source, &QGeoPositionInfoSource::errorOccurred, context, [finish](QGeoPositionInfoSource::Error) {
        finish(error("failed", QStringLiteral("Could not get your location (GPS off or no signal). Search for a city instead.")));
    });
    QTimer::singleShot(20000, context, [finish]() {   // backstop so the UI can never wait forever
        finish(error("timeout", QStringLiteral("Getting your location took too long. Try again or search for a city.")));
    });

    m_source->requestUpdate(15000);   // single fix, no continuous tracking (saves battery)
}
