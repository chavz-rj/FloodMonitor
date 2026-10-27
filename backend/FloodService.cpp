#include "FloodService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <cmath>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#endif

namespace {
QString q(const std::string &s) { return QString::fromStdString(s); }
}

// ---- Saving a copy of a file where the phone's Files app can see it ---------------------------------
//  Android: Downloads/FloodMonitor, through MediaStore (Android 10 and newer, needs no permission).
//  Desktop: the user's Downloads/FloodMonitor folder (so the feature can be tried without a phone).
namespace {
#ifdef Q_OS_ANDROID
QString exportFolderText() { return QStringLiteral("Downloads/FloodMonitor"); }

bool saveCopy(const QString &fileName, const QByteArray &data, QString &error) {
    if (QNativeInterface::QAndroidApplication::sdkVersion() < 29) {
        error = QStringLiteral("Saving to the Downloads folder needs Android 10 or newer.");
        return false;
    }
    QJniEnvironment env;
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    QJniObject resolver = context.callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;");
    QJniObject collection = QJniObject::getStaticObjectField("android/provider/MediaStore$Downloads",
                                                             "EXTERNAL_CONTENT_URI", "Landroid/net/Uri;");
    if (env.checkAndClearExceptions() || !resolver.isValid() || !collection.isValid()) {
        error = QStringLiteral("Could not reach the phone's file storage.");
        return false;
    }

    // Remove the copy this app saved earlier, so the name stays "flood_data.txt" (not "flood_data (1).txt").
    // fileName was checked by the caller: letters, digits, dot, dash and underscore only.
    const QString where = QStringLiteral("_display_name='%1' AND relative_path='Download/FloodMonitor/'").arg(fileName);
    resolver.callMethod<jint>("delete", "(Landroid/net/Uri;Ljava/lang/String;[Ljava/lang/String;)I",
                              collection.object(), QJniObject::fromString(where).object<jstring>(),
                              static_cast<jobject>(nullptr));
    env.checkAndClearExceptions();

    QJniObject values("android/content/ContentValues");
    auto put = [&values](const QString &key, const QString &value) {
        values.callMethod<void>("put", "(Ljava/lang/String;Ljava/lang/String;)V",
                                QJniObject::fromString(key).object<jstring>(),
                                QJniObject::fromString(value).object<jstring>());
    };
    put(QStringLiteral("_display_name"), fileName);
    put(QStringLiteral("mime_type"), QStringLiteral("text/plain"));
    put(QStringLiteral("relative_path"), QStringLiteral("Download/FloodMonitor/"));

    QJniObject uri = resolver.callObjectMethod("insert", "(Landroid/net/Uri;Landroid/content/ContentValues;)Landroid/net/Uri;",
                                               collection.object(), values.object());
    if (env.checkAndClearExceptions() || !uri.isValid()) {
        error = QStringLiteral("The phone would not create the file.");
        return false;
    }
    QJniObject stream = resolver.callObjectMethod("openOutputStream", "(Landroid/net/Uri;Ljava/lang/String;)Ljava/io/OutputStream;",
                                                  uri.object(), QJniObject::fromString(QStringLiteral("wt")).object<jstring>());
    if (env.checkAndClearExceptions() || !stream.isValid()) {
        error = QStringLiteral("The phone would not open the file for writing.");
        return false;
    }
    jbyteArray array = env->NewByteArray(data.size());
    env->SetByteArrayRegion(array, 0, data.size(), reinterpret_cast<const jbyte *>(data.constData()));
    stream.callMethod<void>("write", "([B)V", array);
    stream.callMethod<void>("close", "()V");
    env->DeleteLocalRef(array);
    if (env.checkAndClearExceptions()) {
        error = QStringLiteral("Writing the file failed.");
        return false;
    }
    return true;
}
#else
QString exportFolder() { return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + QStringLiteral("/FloodMonitor"); }
QString exportFolderText() { return QDir::toNativeSeparators(exportFolder()); }

bool saveCopy(const QString &fileName, const QByteArray &data, QString &error) {
    if (!QDir().mkpath(exportFolder())) {
        error = QStringLiteral("Could not create the folder %1.").arg(exportFolderText());
        return false;
    }
    QFile f(exportFolder() + QLatin1Char('/') + fileName);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(data) != data.size()) {
        error = QStringLiteral("Could not write %1.").arg(fileName);
        return false;
    }
    return true;
}
#endif
}

FloodService::FloodService(const QString &dataDir, QObject *parent) : QObject(parent) {
    QDir().mkpath(dataDir);
    m_dataDir = QDir(dataDir).absolutePath();
    setDataDirectory(QDir(dataDir).absolutePath().toStdString() + "/");
    initSegments(m_names, m_levels, m_times, m_alert, m_critical);
    m_loadResult = loadData(m_levels, m_times);
}

QJsonObject FloodService::fail(const QString &code, const QString &message) {
    QJsonObject o;
    o["ok"] = false;
    o["code"] = code;
    o["message"] = message;
    return o;
}

QJsonObject FloodService::startup() const {
    QJsonObject o;
    o["ok"] = true;
    o["loadResult"] = m_loadResult;   // 0 ok, 1 no file, 2 bad file (same codes as the console version)
    o["operator"] = q(getOperatorName());
    o["maxOperatorLength"] = MAX_OPERATOR_NAME_LENGTH;
    o["maxDepth"] = MAX_DEPTH;
    o["outdatedAfterMinutes"] = OUTDATED_AFTER_MINUTES;
    return o;
}

QJsonObject FloodService::segmentJson(int i, std::time_t now) const {
    const int id = i + 1;
    const bool has = hasReading(m_levels[i]);
    const int status = getStatus(m_levels[i], m_times[i], now, m_alert[i], m_critical[i]);

    double rate = 0.0;
    const int trend = getSegmentTrend(id, m_levels[i], m_times[i], rate);

    QJsonObject o;
    o["id"] = id;
    o["name"] = q(m_names[i]);
    o["hasReading"] = has;
    o["level"] = has ? QJsonValue(m_levels[i]) : QJsonValue(QJsonValue::Null);
    o["levelText"] = q(formatLevel(m_levels[i]));
    o["alertLimit"] = m_alert[i];
    o["criticalLimit"] = m_critical[i];
    o["updatedText"] = has ? q(formatTimestamp(m_times[i])) : QStringLiteral("never");
    o["ageText"] = has ? q(formatAge(m_times[i], now)) : QString();
    o["status"] = status;
    o["statusLabel"] = q(getStatusLabel(status));
    o["statusMessage"] = q(getStatusMessage(status));
    o["oldReadingNote"] = isReadingOutdated(m_levels[i], m_times[i], now) && status != STATUS_OUTDATED;
    o["trend"] = trend;                                   // 0 unknown 1 steady 2 rising 3 falling
    o["rate"] = std::fabs(rate);
    o["trendLine"] = q(getTrendLine(id, m_levels[i], m_times[i]));
    o["projectionLine"] = q(getProjectionLine(id, m_levels[i], m_times[i], m_alert[i], m_critical[i], now));
    o["text"] = q(describeSegment(i, m_names, m_levels, m_times, m_alert, m_critical, now));
    return o;
}

QJsonObject FloodService::board() const {
    const std::time_t now = std::time(0);
    QJsonArray segments;
    for (int i = 0; i < NUM_SEGMENTS; i++)
        segments.append(segmentJson(i, now));

    int counts[STATUS_COUNT];
    countStatuses(m_levels, m_times, now, m_alert, m_critical, counts);
    const int highest = findHighestRiskIndex(m_levels, m_alert, m_critical);

    QJsonObject c;
    c["noData"] = counts[STATUS_NO_DATA];
    c["outdated"] = counts[STATUS_OUTDATED];
    c["safe"] = counts[STATUS_SAFE];
    c["alert"] = counts[STATUS_ALERT];
    c["critical"] = counts[STATUS_CRITICAL];

    QJsonObject o;
    o["ok"] = true;
    o["segments"] = segments;
    o["counts"] = c;
    o["overall"] = getOverallStatus(counts);   // 0 all safe, 1 incomplete, 2 alert, 3 critical
    o["overallMessage"] = q(getOverallMessage(getOverallStatus(counts)));
    if (highest >= 0) {
        const int st = getStatus(m_levels[highest], m_times[highest], now, m_alert[highest], m_critical[highest]);
        o["highestName"] = q(m_names[highest]);
        o["highestMessage"] = q(getStatusMessage(st));
    } else {
        o["highestName"] = QJsonValue(QJsonValue::Null);
        o["highestMessage"] = QString();
    }
    o["operator"] = q(getOperatorName());
    o["generatedText"] = q(formatTimestamp(now));
    return o;
}

QJsonObject FloodService::updateLevel(int segmentId, const QString &depthText, bool confirmed) {
    if (!isValidSegmentId(segmentId))
        return fail("badSegment", QStringLiteral("Invalid Segment ID. Choose a segment from 1 to %1.").arg(NUM_SEGMENTS));

    double depth = 0.0;
    if (!parseDouble(depthText.trimmed().toStdString(), depth) || !isValidDepth(depth))
        return fail("badDepth", QStringLiteral("Invalid depth. Enter a number from 0.00 to %1 (example: 0.45). Nothing was changed.")
                                    .arg(MAX_DEPTH, 0, 'f', 2));

    const std::time_t now = std::time(0);
    const int index = segmentId - 1;

    if (!confirmed && isSuspiciousChange(m_levels[index], m_times[index], depth, now)) {
        QJsonObject o;
        o["ok"] = false;
        o["needsConfirm"] = true;
        o["message"] = QStringLiteral("The last reading was %1 (%2). The new value %3 m is a very big change. Is the new depth correct?")
                           .arg(q(formatLevel(m_levels[index])), q(formatAge(m_times[index], now)))
                           .arg(roundToCentimeter(depth), 0, 'f', 2);
        return o;
    }

    const int result = recordReading(segmentId, depth, m_names, m_levels, m_times, m_alert, m_critical, now);
    if (result == READING_BAD_CLOCK)
        return fail("badClock", QStringLiteral("The device clock is unavailable. Nothing was changed."));
    if (!isReadingAccepted(result))
        return fail("rejected", QStringLiteral("The reading was rejected. Nothing was changed."));

    QJsonArray warnings;
    if (result == READING_NOT_SAVED)
        warnings.append(QStringLiteral("The new value could NOT be saved to the data file and will be lost when the app closes."));
    if (result == READING_NOT_LOGGED)
        warnings.append(QStringLiteral("The update history files could not be written."));

    QJsonObject o;
    o["ok"] = true;
    o["code"] = result;
    o["message"] = QStringLiteral("%1 updated to %2 m by %3.")
                       .arg(q(m_names[index])).arg(m_levels[index], 0, 'f', 2).arg(q(getOperatorName()));
    o["warnings"] = warnings;
    o["segment"] = segmentJson(index, now);
    return o;
}

QJsonObject FloodService::setOperator(const QString &name) {
    if (!setOperatorName(name.toStdString()))
        return fail("badName", QStringLiteral("Invalid name. Use 1 to %1 characters and no \"|\" sign.").arg(MAX_OPERATOR_NAME_LENGTH));
    QJsonObject o;
    o["ok"] = true;
    o["operator"] = q(getOperatorName());
    return o;
}

QJsonObject FloodService::history() const {
    std::string lines[RECENT_LOG_LIMIT];
    const int count = getRecentHistory(lines);
    QJsonArray arr;
    for (int i = 0; i < count; i++)
        arr.append(q(lines[i]));
    QJsonObject o;
    o["ok"] = true;
    o["available"] = count >= 0;   // false = no history file yet
    o["limit"] = RECENT_LOG_LIMIT;
    o["lines"] = arr;
    return o;
}

QJsonObject FloodService::statistics() const {
    int readingCounts[NUM_SEGMENTS];
    double peakLevels[NUM_SEGMENTS];
    std::time_t peakTimes[NUM_SEGMENTS];
    int warningCounts[NUM_SEGMENTS];
    const int total = computeStatistics(m_alert, m_critical, readingCounts, peakLevels, peakTimes, warningCounts);

    QJsonArray segs;
    for (int i = 0; i < NUM_SEGMENTS; i++) {
        QJsonObject s;
        s["id"] = i + 1;
        s["name"] = q(m_names[i]);
        s["readings"] = readingCounts[i];
        s["warnings"] = warningCounts[i];
        s["peakText"] = readingCounts[i] > 0 ? q(formatLevel(peakLevels[i])) : QString();
        s["peakTime"] = readingCounts[i] > 0 ? q(formatTimestamp(peakTimes[i])) : QString();
        segs.append(s);
    }
    QJsonObject o;
    o["ok"] = true;
    o["total"] = total < 0 ? 0 : total;
    o["segments"] = segs;
    return o;
}

QJsonObject FloodService::report() {
    const std::time_t now = std::time(0);
    const std::string text = buildSituationReport(m_names, m_levels, m_times, m_alert, m_critical, now);
    const std::string path = saveSituationReport(text, now);

    QJsonObject o;
    o["ok"] = true;
    o["text"] = q(text);
    o["saved"] = !path.empty();
    o["fileName"] = path.empty() ? QString() : QFileInfo(q(path)).fileName();
    return o;
}

// Copies every saved data file (flood_data.txt, flood_log.txt, reports, ...) to a folder the user can open.
QJsonObject FloodService::exportFiles() {
    const QStringList filters = {QStringLiteral("*.txt"), QStringLiteral("flood_*")};
    const QFileInfoList files = QDir(m_dataDir).entryInfoList(filters, QDir::Files, QDir::Name);
    const QRegularExpression safeName(QStringLiteral("^[A-Za-z0-9._-]+$"));

    int saved = 0;
    QStringList failed;
    QString lastError;
    for (const QFileInfo &info : files) {
        const QString name = info.fileName();
        if (name.endsWith(QLatin1String(".tmp")) || !safeName.match(name).hasMatch())
            continue;
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            failed.append(name);
            continue;
        }
        QString error;
        if (saveCopy(name, file.readAll(), error)) {
            saved++;
        } else {
            failed.append(name);
            lastError = error;
        }
    }

    if (saved == 0 && failed.isEmpty())
        return fail("nothing", QStringLiteral("There are no data files yet. Record a reading first."));
    if (saved == 0)
        return fail("exportFailed", lastError.isEmpty() ? QStringLiteral("The files could not be saved.") : lastError);

    QJsonObject o;
    o["ok"] = true;
    o["count"] = saved;
    o["folder"] = exportFolderText();
    o["failed"] = QJsonArray::fromStringList(failed);
    o["message"] = QStringLiteral("Saved %1 file(s) to %2.").arg(saved).arg(exportFolderText());
    return o;
}