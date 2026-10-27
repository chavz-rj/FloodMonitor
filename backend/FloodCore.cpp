#define _CRT_SECURE_NO_WARNINGS
#include "FloodCore.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include <cmath>
#include <ctime>
#include <cstdio>

using namespace std;


string operatorName = "unknown";

// ADDED for mobile
static string g_dataDirectory = "";


// PART 2 - CONSTANTS AND ARRAYS

void initSegments(string names[], double levels[], time_t times[],
                  double alertLimits[], double criticalLimits[]) {
    names[0] = "JP Laurel - Robinsons Area";
    names[1] = "JP Laurel - De La Salle Lipa Gate";
    names[2] = "JP Laurel - Tambo Exit";

    alertLimits[0] = 0.4;
    alertLimits[1] = 0.3;
    alertLimits[2] = 0.5;

    criticalLimits[0] = 0.8;
    criticalLimits[1] = 0.6;
    criticalLimits[2] = 1.0;

    for (int i = 0; i < NUM_SEGMENTS; i++) {
        levels[i] = NO_READING;
        times[i] = 0;
    }
}


// PART 3 - INPUT VALIDATION


bool parseDouble(const string &text, double &value) {
    stringstream parser(text);
    char extra;
    return (parser >> value) && !(parser >> extra);
}

bool isValidSegmentId(int segmentId) {
    return segmentId >= 1 && segmentId <= NUM_SEGMENTS;
}

bool isValidDepth(double depth) {
    return depth >= 0.0 && depth <= MAX_DEPTH;
}

double roundToCentimeter(double depth) {
    return floor(depth * 100.0 + 0.5) / 100.0;
}

bool setOperatorName(const string &name) {
    if (name.size() == 0 || (int)name.size() > MAX_OPERATOR_NAME_LENGTH) {
        return false;
    }

    bool hasText = false;
    for (int i = 0; i < (int)name.size(); i++) {
        unsigned char c = name[i];
        if (c == '|' || c < 32 || c == 127) {
            return false;
        }
        if (c != ' ') {
            hasText = true;
        }
    }
    if (!hasText) {
        return false;
    }

    operatorName = name;
    return true;
}

// Helpers
bool parseInt(const string &text, int &value) {
    stringstream parser(text);
    char extra;
    return (parser >> value) && !(parser >> extra);
}

int toCentimeters(double meters) {
    return (int)floor(meters * 100.0 + 0.5);
}


// PART 4 - STATUS OF A SEGMENT


bool isReadingOutdated(double level, time_t when, time_t now) {
    if (!hasReading(level)) {
        return false;
    }
    double age = getReadingAge(when, now);
    return age < 0.0 || age > OUTDATED_AFTER_MINUTES * 60;
}

int getDepthStatus(double level, double alertLimit, double criticalLimit) {
    if (!hasReading(level)) {
        return STATUS_NO_DATA;
    } else if (level >= criticalLimit) {
        return STATUS_CRITICAL;
    } else if (level >= alertLimit) {
        return STATUS_ALERT;
    } else {
        return STATUS_SAFE;
    }
}

int getStatus(double level, time_t when, time_t now,
              double alertLimit, double criticalLimit) {
    int status = getDepthStatus(level, alertLimit, criticalLimit);

    if (status == STATUS_SAFE && isReadingOutdated(level, when, now)) {
        return STATUS_OUTDATED;
    }
    return status;
}

// Helpers
bool hasReading(double level) {
    return level >= 0.0;
}

double getReadingAge(time_t when, time_t now) {
    return difftime(now, when);
}

string getStatusLabel(int status) {
    if (status == STATUS_SAFE) {
        return "SAFE";
    } else if (status == STATUS_ALERT) {
        return "ALERT";
    } else if (status == STATUS_CRITICAL) {
        return "CRITICAL";
    } else if (status == STATUS_OUTDATED) {
        return "OUTDATED";
    } else {
        return "NO DATA";
    }
}

string getStatusMessage(int status) {
    if (status == STATUS_SAFE) {
        return "STATUS: [SAFE - CLEAR PASSAGE]";
    } else if (status == STATUS_ALERT) {
        return "STATUS: [ALERT - DRIVE WITH CAUTION]";
    } else if (status == STATUS_CRITICAL) {
        return "STATUS: [CRITICAL - IMPASSABLE / REROUTE IMMEDIATELY]";
    } else if (status == STATUS_OUTDATED) {
        return "STATUS: [OUTDATED - READING IS OLD, DO NOT ASSUME SAFE]";
    } else {
        return "STATUS: [NO DATA - NOT YET MEASURED, TREAT AS UNKNOWN]";
    }
}


// PART 5 - OVERALL SUMMARY

void countStatuses(const double levels[], const time_t times[], time_t now,
                   const double alertLimits[], const double criticalLimits[], int counts[]) {
    for (int s = 0; s < STATUS_COUNT; s++) {
        counts[s] = 0;
    }

    for (int i = 0; i < NUM_SEGMENTS; i++) {
        int status = getStatus(levels[i], times[i], now, alertLimits[i], criticalLimits[i]);
        counts[status]++;
    }
}

int findHighestRiskIndex(const double levels[], const double alertLimits[],
                         const double criticalLimits[]) {
    int highestStatus = STATUS_NO_DATA;
    int highestIndex = -1;

    for (int i = 0; i < NUM_SEGMENTS; i++) {
        int status = getDepthStatus(levels[i], alertLimits[i], criticalLimits[i]);

        if (status > highestStatus) {
            highestStatus = status;
            highestIndex = i;
        }
    }
    return highestIndex;
}

int getOverallStatus(const int counts[]) {
    if (counts[STATUS_CRITICAL] > 0) {
        return OVERALL_CRITICAL;
    } else if (counts[STATUS_ALERT] > 0) {
        return OVERALL_ALERT;
    } else if (counts[STATUS_NO_DATA] > 0 || counts[STATUS_OUTDATED] > 0) {
        return OVERALL_INCOMPLETE;
    } else {
        return OVERALL_ALL_SAFE;
    }
}

// Helpers

string getOverallMessage(int overall) {
    if (overall == OVERALL_CRITICAL) {
        return "OVERALL: CRITICAL - at least one segment needs immediate review.";
    } else if (overall == OVERALL_ALERT) {
        return "OVERALL: ALERT - use caution and review the affected segment.";
    } else if (overall == OVERALL_INCOMPLETE) {
        return "OVERALL: INCOMPLETE - a segment has no reading or an outdated one. "
               "Do not assume every segment is safe.";
    } else {
        return "OVERALL: ALL SEGMENTS HAVE A RECENT READING BELOW ALERT LEVEL.";
    }
}


// PART 6 - RECORDING A READING

int recordReading(int segmentId, double depth, const string names[],
                  double levels[], time_t times[],
                  const double alertLimits[], const double criticalLimits[],
                  time_t now) {
    if (!isValidSegmentId(segmentId)) {
        return READING_BAD_SEGMENT;
    }
    if (!isValidDepth(depth)) {
        return READING_BAD_DEPTH;
    }
    if (now < 0) {
        return READING_BAD_CLOCK;
    }

    int index = segmentId - 1;
    depth = roundToCentimeter(depth);
    levels[index] = depth;
    times[index] = now;

    bool saved = saveData(levels, times);
    int status = getStatus(levels[index], times[index], now,
                           alertLimits[index], criticalLimits[index]);
    bool logged = appendLog(names[index], depth, status, now);
    bool remembered = appendReadingHistory(segmentId, depth, now);

    if (!saved) {
        return READING_NOT_SAVED;
    }
    if (!logged || !remembered) {
        return READING_NOT_LOGGED;
    }
    return READING_OK;
}

bool isReadingAccepted(int resultCode) {
    return resultCode == READING_OK
           || resultCode == READING_NOT_SAVED
           || resultCode == READING_NOT_LOGGED;
}


// PART 7 - SAVING AND LOADING


bool saveData(const double levels[], const time_t times[]) {
    string finalPath = getDataFilePath();
    string tempPath = finalPath + ".tmp";

    ofstream outFile(tempPath.c_str());
    if (!outFile) {
        return false;
    }

    outFile << fixed << setprecision(2);
    for (int i = 0; i < NUM_SEGMENTS; i++) {
        outFile << levels[i] << " " << times[i] << "\n";
    }

    outFile.close();
    if (outFile.fail()) {
        remove(tempPath.c_str());
        return false;
    }

#ifdef _WIN32
    remove(finalPath.c_str());   // Windows rename() will not replace an existing file
#endif
    if (rename(tempPath.c_str(), finalPath.c_str()) != 0) {
        remove(tempPath.c_str());
        return false;
    }
    return true;
}

int loadData(double levels[], time_t times[]) {
    for (int i = 0; i < NUM_SEGMENTS; i++) {
        levels[i] = NO_READING;
        times[i] = 0;
    }

    ifstream inFile(getDataFilePath().c_str());
    if (!inFile) {
        return LOAD_NO_FILE;
    }

    double loadedLevels[NUM_SEGMENTS];
    time_t loadedTimes[NUM_SEGMENTS];
    string line;

    for (int i = 0; i < NUM_SEGMENTS; i++) {
        if (!getline(inFile, line)) {
            return LOAD_BAD_FILE;
        }

        stringstream parser(line);
        char extra;
        if (!(parser >> loadedLevels[i] >> loadedTimes[i]) || (parser >> extra)) {
            return LOAD_BAD_FILE;
        }

        bool levelIsGood = (loadedLevels[i] == NO_READING) || isValidDepth(loadedLevels[i]);
        if (!levelIsGood || loadedTimes[i] < 0) {
            return LOAD_BAD_FILE;
        }
    }

    while (getline(inFile, line)) {
        if (line.find_first_not_of(" \t\r") != string::npos) {
            return LOAD_BAD_FILE;
        }
    }

    for (int i = 0; i < NUM_SEGMENTS; i++) {
        if (loadedLevels[i] != NO_READING) {
            levels[i] = roundToCentimeter(loadedLevels[i]);
            times[i] = loadedTimes[i];
        }
    }
    return LOAD_OK;
}

bool appendLog(const string &segmentName, double depth, int status, time_t when) {
    ofstream logFile(getLogFilePath().c_str(), ios::app);
    if (!logFile) {
        return false;
    }

    logFile << formatTimestamp(when) << " | " << segmentName << " | "
            << fixed << setprecision(2) << depth << " m | "
            << getStatusMessage(status) << " | by " << operatorName << "\n";

    logFile.close();
    return !logFile.fail();
}

bool appendReadingHistory(int segmentId, double depth, time_t when) {
    ofstream historyFile(getHistoryFilePath().c_str(), ios::app);
    if (!historyFile) {
        return false;
    }

    historyFile << segmentId << " " << fixed << setprecision(2) << depth << " " << when << "\n";

    historyFile.close();
    return !historyFile.fail();
}

// Helpers

string getDataFilePath() {
    return g_dataDirectory + "flood_data.txt";
}

string getLogFilePath() {
    return g_dataDirectory + "flood_log.txt";
}

string getHistoryFilePath() {
    return g_dataDirectory + "flood_readings.txt";
}


// PART 8 - TREND AND ESTIMATE

bool findLastTwoReadings(int segmentId, double &prevLevel, time_t &prevTime,
                         double &lastLevel, time_t &lastTime) {
    prevLevel = NO_READING;
    prevTime = 0;
    lastLevel = NO_READING;
    lastTime = 0;

    ifstream historyFile(getHistoryFilePath().c_str());
    if (!historyFile) {
        return false;
    }

    int found = 0;
    string line;
    int id = 0;
    double level = 0.0;
    time_t when = 0;

    while (getline(historyFile, line)) {
        if (parseHistoryLine(line, id, level, when) && id == segmentId) {
            prevLevel = lastLevel;
            prevTime = lastTime;
            lastLevel = level;
            lastTime = when;
            found++;
        }
    }
    return found >= 2;
}

int getSegmentTrend(int segmentId, double level, time_t when, double &rate) {
    rate = 0.0;

    if (!isValidSegmentId(segmentId) || !hasReading(level)) {
        return TREND_UNKNOWN;
    }

    double prevLevel, lastLevel;
    time_t prevTime, lastTime;
    if (!findLastTwoReadings(segmentId, prevLevel, prevTime, lastLevel, lastTime)) {
        return TREND_UNKNOWN;
    }

    if (fabs(lastLevel - level) > 0.001 || lastTime != when) {
        return TREND_UNKNOWN;
    }

    return classifyTrend(prevLevel, prevTime, level, when, rate);
}

int classifyTrend(double prevLevel, time_t prevTime, double level, time_t when,
                  double &rate) {
    rate = 0.0;

    if (!hasReading(prevLevel) || !hasReading(level)) {
        return TREND_UNKNOWN;
    }

    double minutes = difftime(when, prevTime) / 60.0;
    if (minutes < TREND_MIN_MINUTES || minutes > TREND_MAX_MINUTES) {
        return TREND_UNKNOWN;
    }

    int changeCm = toCentimeters(level - prevLevel);
    rate = (changeCm / 100.0) / (minutes / 60.0);

    if (changeCm >= TREND_NOISE_CM) {
        return TREND_RISING;
    } else if (changeCm <= -TREND_NOISE_CM) {
        return TREND_FALLING;
    } else {
        return TREND_STEADY;
    }
}

int projectMinutesToLimit(double level, time_t when, double rate, double limit,
                          time_t now) {
    if (!hasReading(level) || rate <= 0.0 || level >= limit) {
        return -1;
    }
    if (isReadingOutdated(level, when, now)) {
        return -1;
    }

    double minutesFromReading = (limit - level) / rate * 60.0;
    if (minutesFromReading > PROJECTION_MAX_MINUTES) {
        return -1;
    }

    double minutesLeft = minutesFromReading - getReadingAge(when, now) / 60.0;
    if (minutesLeft < 0.0) {
        minutesLeft = 0.0;
    }
    return (int)(minutesLeft + 0.5);
}

// ---- Helpers for this part (not named in the script) ---------------------

bool parseHistoryLine(const string &line, int &segmentId, double &level, time_t &when) {
    stringstream parser(line);
    char extra;

    if (!(parser >> segmentId >> level >> when) || (parser >> extra)) {
        return false;
    }
    return isValidSegmentId(segmentId) && isValidDepth(level) && when >= 0;
}

string formatRate(double rate) {
    stringstream text;
    text << fixed << setprecision(2) << fabs(rate) << " m/h";
    return text.str();
}

string getTrendMessage(int trend, double rate) {
    if (trend == TREND_RISING) {
        return "TREND: RISING at about " + formatRate(rate);
    } else if (trend == TREND_FALLING) {
        return "TREND: FALLING at about " + formatRate(rate);
    } else if (trend == TREND_STEADY) {
        return "TREND: STEADY (no clear change since the previous reading)";
    } else {
        return "TREND: not available (needs two readings " + intToString(TREND_MIN_MINUTES)
        + " to " + intToString(TREND_MAX_MINUTES) + " minutes apart)";
    }
}

string getTrendLine(int segmentId, double level, time_t when) {
    if (!hasReading(level)) {
        return "";
    }

    double rate = 0.0;
    int trend = getSegmentTrend(segmentId, level, when, rate);
    return getTrendMessage(trend, rate);
}

int getNextTargetStatus(double level, double alertLimit, double criticalLimit) {
    if (!hasReading(level)) {
        return NO_TARGET;
    } else if (level < alertLimit) {
        return STATUS_ALERT;
    } else if (level < criticalLimit) {
        return STATUS_CRITICAL;
    } else {
        return NO_TARGET;
    }
}

string getProjectionMessage(int targetStatus, int minutes) {
    if (targetStatus == NO_TARGET || minutes < 0) {
        return "";
    }

    string label = getStatusLabel(targetStatus);
    if (minutes == 0) {
        return "ESTIMATE: at this rate the " + label + " limit may already be reached - verify on site.";
    }
    return "ESTIMATE: at this rate the " + label + " limit could be reached in about "
           + intToString(minutes) + " min.";
}

string getProjectionLine(int segmentId, double level, time_t when,
                         double alertLimit, double criticalLimit, time_t now) {
    double rate = 0.0;
    int trend = getSegmentTrend(segmentId, level, when, rate);
    if (trend != TREND_RISING) {
        return "";
    }

    int target = getNextTargetStatus(level, alertLimit, criticalLimit);
    if (target == NO_TARGET) {
        return "";
    }

    double limit = criticalLimit;
    if (target == STATUS_ALERT) {
        limit = alertLimit;
    }

    int minutes = projectMinutesToLimit(level, when, rate, limit, now);
    return getProjectionMessage(target, minutes);
}


// PART 9 - SUSPICIOUS CHANGE

bool isSuspiciousChange(double oldLevel, time_t oldTime, double newDepth, time_t now) {
    if (!isValidDepth(newDepth)) {
        return false;
    }
    if (!hasReading(oldLevel) || isReadingOutdated(oldLevel, oldTime, now)) {
        return false;
    }

    int changeCm = toCentimeters(newDepth) - toCentimeters(oldLevel);
    if (changeCm < 0) {
        changeCm = -changeCm;
    }
    return changeCm >= SUSPICIOUS_CHANGE_CM;
}


// PART 10 - HISTORY, STATISTICS, REPORT

int getRecentHistory(string lines[]) {
    ifstream logFile(getLogFilePath().c_str());
    if (!logFile) {
        return -1;
    }

    int count = 0;
    string line;

    while (getline(logFile, line)) {
        if (line != "") {
            if (count < RECENT_LOG_LIMIT) {
                lines[count] = line;
                count++;
            } else {
                for (int i = 0; i < RECENT_LOG_LIMIT - 1; i++) {
                    lines[i] = lines[i + 1];
                }
                lines[RECENT_LOG_LIMIT - 1] = line;
            }
        }
    }
    return count;
}

int computeStatistics(const double alertLimits[], const double criticalLimits[],
                      int readingCounts[], double peakLevels[], time_t peakTimes[],
                      int warningCounts[]) {
    for (int i = 0; i < NUM_SEGMENTS; i++) {
        readingCounts[i] = 0;
        peakLevels[i] = NO_READING;
        peakTimes[i] = 0;
        warningCounts[i] = 0;
    }

    ifstream historyFile(getHistoryFilePath().c_str());
    if (!historyFile) {
        return -1;
    }

    int total = 0;
    string line;
    int segmentId = 0;
    double level = 0.0;
    time_t when = 0;

    while (getline(historyFile, line)) {
        if (parseHistoryLine(line, segmentId, level, when)) {
            int i = segmentId - 1;
            readingCounts[i]++;
            total++;

            if (level > peakLevels[i]) {
                peakLevels[i] = level;
                peakTimes[i] = when;
            }

            int status = getDepthStatus(level, alertLimits[i], criticalLimits[i]);
            if (status == STATUS_ALERT || status == STATUS_CRITICAL) {
                warningCounts[i]++;
            }
        }
    }
    return total;
}

string describeSegment(int index, const string names[], const double levels[],
                       const time_t times[], const double alertLimits[],
                       const double criticalLimits[], time_t now) {
    stringstream text;
    text << fixed << setprecision(2);
    int segmentId = index + 1;

    text << "Segment " << segmentId << ": " << names[index] << "\n";
    text << "Water Level: " << formatLevel(levels[index])
         << " | Alert Limit: " << alertLimits[index] << " m"
         << " | Critical Limit: " << criticalLimits[index] << " m\n";

    if (!hasReading(levels[index])) {
        text << "Last Updated: never\n";
    } else {
        text << "Last Updated: " << formatTimestamp(times[index])
        << " (" << formatAge(times[index], now) << ")\n";
    }

    int status = getStatus(levels[index], times[index], now,
                           alertLimits[index], criticalLimits[index]);
    text << getStatusMessage(status) << "\n";

    if (isReadingOutdated(levels[index], times[index], now) && status != STATUS_OUTDATED) {
        text << "NOTE: this reading is older than " << OUTDATED_AFTER_MINUTES
             << " minutes. Please verify and update it.\n";
    }

    string trendLine = getTrendLine(segmentId, levels[index], times[index]);
    if (trendLine != "") {
        text << trendLine << "\n";
    }

    string projectionLine = getProjectionLine(segmentId, levels[index], times[index],
                                              alertLimits[index], criticalLimits[index], now);
    if (projectionLine != "") {
        text << projectionLine << "\n";
    }

    return text.str();
}

string buildSituationReport(const string names[], const double levels[],
                            const time_t times[], const double alertLimits[],
                            const double criticalLimits[], time_t now) {
    stringstream report;
    int counts[STATUS_COUNT];
    countStatuses(levels, times, now, alertLimits, criticalLimits, counts);

    report << "JP LAUREL HIGHWAY FLOOD SITUATION REPORT\n";
    report << "Generated   : " << formatTimestamp(now) << "\n";
    report << "Prepared by : " << operatorName << "\n";
    report << "============================================================\n";
    report << getOverallMessage(getOverallStatus(counts)) << "\n";
    report << "No data: " << counts[STATUS_NO_DATA]
           << " | Outdated: " << counts[STATUS_OUTDATED]
           << " | Safe: " << counts[STATUS_SAFE]
           << " | Alert: " << counts[STATUS_ALERT]
           << " | Critical: " << counts[STATUS_CRITICAL] << "\n";
    report << "============================================================\n";

    for (int i = 0; i < NUM_SEGMENTS; i++) {
        report << describeSegment(i, names, levels, times, alertLimits, criticalLimits, now);
        report << "------------------------------------------------------------\n";
    }

    report << "Depths are manual estimates entered by enforcers. Trend and estimate\n";
    report << "lines are guides, not forecasts. Verify on site before acting.\n";
    return report.str();
}

string saveSituationReport(const string &reportText, time_t now) {
    char stamp[32] = "unknown_time";
    tm *localTime = localtime(&now);
    if (localTime != 0) {
        strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", localTime);
    }

    string path = g_dataDirectory + "flood_report_" + string(stamp) + ".txt";
    ofstream reportFile(path.c_str());
    if (!reportFile) {
        return "";
    }

    reportFile << reportText;
    reportFile.close();

    if (reportFile.fail()) {
        return "";
    }
    return path;
}

// Helpers

string intToString(int value) {
    stringstream converter;
    converter << value;
    return converter.str();
}

string formatLevel(double level) {
    if (!hasReading(level)) {
        return "no reading";
    }
    stringstream text;
    text << fixed << setprecision(2) << level << " m";
    return text.str();
}

string formatTimestamp(time_t when) {
    char stamp[32] = "unknown time";
    tm *localTime = localtime(&when);
    if (localTime != 0) {
        strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", localTime);
    }
    return string(stamp);
}

string formatAge(time_t when, time_t now) {
    double seconds = getReadingAge(when, now);

    if (seconds < 0) {
        return "time unclear - check the computer clock";
    }
    if (seconds > 365.0 * 24 * 60 * 60) {
        return "over a year ago";
    }
    if (seconds < 60) {
        return "just now";
    }

    int minutes = (int)(seconds / 60.0);
    if (minutes < 60) {
        return intToString(minutes) + " min ago";
    }
    return intToString(minutes / 60) + " h " + intToString(minutes % 60) + " min ago";
}


// PART 11 - FRONTEND (QT BRIDGE)

void setDataDirectory(const std::string &directoryWithTrailingSlash) {
    g_dataDirectory = directoryWithTrailingSlash;
}

std::string getOperatorName() {
    return operatorName;
}