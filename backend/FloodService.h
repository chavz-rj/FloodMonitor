// FloodService - Qt-side wrapper around the original flood logic (FloodCore).
// Holds the same arrays the console program kept in main() and returns JSON for the web UI.
#pragma once
#include <QObject>
#include <QJsonObject>
#include <QString>
#include <string>
#include <ctime>
#include "FloodCore.h"

class FloodService : public QObject {
    Q_OBJECT
public:
    explicit FloodService(const QString &dataDir, QObject *parent = nullptr);

    QJsonObject startup() const;                       // load result + current operator
    QJsonObject board() const;                         // menu 1 + 3: segments, counts, overall summary
    QJsonObject updateLevel(int segmentId, const QString &depthText, bool confirmed); // menu 2
    QJsonObject setOperator(const QString &name);      // startup "enter your name"
    QJsonObject history() const;                       // menu 4
    QJsonObject statistics() const;                    // menu 5
    QJsonObject report();                              // menu 6
    QJsonObject exportFiles();                         // copy the data files where the phone's Files app can see them

private:
    QJsonObject segmentJson(int index, std::time_t now) const;
    static QJsonObject fail(const QString &code, const QString &message);

    std::string m_names[NUM_SEGMENTS];
    double m_levels[NUM_SEGMENTS];
    std::time_t m_times[NUM_SEGMENTS];
    double m_alert[NUM_SEGMENTS];
    double m_critical[NUM_SEGMENTS];
    int m_loadResult = LOAD_OK;
    QString m_dataDir;
};