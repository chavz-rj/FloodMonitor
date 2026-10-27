// LocationHelper - one-shot device location using Qt Positioning and Qt's permission API (Qt 6.5+).
// Asks for permission only when locate() is called (i.e. the user tapped "Use my location").
#pragma once
#include <QObject>
#include <QJsonObject>
#include <functional>
#include <memory>

class QGeoPositionInfoSource;

class LocationHelper : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(const QJsonObject &)>;
    explicit LocationHelper(QObject *parent = nullptr);
    void locate(Callback callback);   // callback always runs exactly once, on the main thread

private:
    void startRequest(Callback callback);
    static QJsonObject error(const QString &code, const QString &message);

    QGeoPositionInfoSource *m_source = nullptr;
    bool m_busy = false;
};
