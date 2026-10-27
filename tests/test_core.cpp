// Tests for the original flood logic (FloodCore). Plain C++, no Qt, no framework.
// Build:  g++ -std=c++17 -Wall -Wextra -Ibackend tests/test_core.cpp backend/FloodCore.cpp -o test_core
#include "FloodCore.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

using namespace std;

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static string names[NUM_SEGMENTS];
static double levels[NUM_SEGMENTS], alertL[NUM_SEGMENTS], critL[NUM_SEGMENTS];
static time_t times[NUM_SEGMENTS];

static void freshState(const string &dir) {
    remove((dir + "flood_data.txt").c_str());
    remove((dir + "flood_log.txt").c_str());
    remove((dir + "flood_readings.txt").c_str());
    initSegments(names, levels, times, alertL, critL);
}

int main() {
    // unique temporary folder (works on Windows and Linux)
    error_code ec;
    filesystem::path tmp = filesystem::temp_directory_path(ec);
    if (ec) {
        printf("Cannot find a temp directory: %s\n", ec.message().c_str());
        return 1;
    }
    tmp /= "floodtest" + to_string(chrono::steady_clock::now().time_since_epoch().count());
    filesystem::create_directories(tmp, ec);
    if (ec) {
        printf("Cannot create temp directory: %s\n", ec.message().c_str());
        return 1;
    }
    string dir = tmp.string() + "/";
    setDataDirectory(dir);
    const time_t T0 = 1760000000;   // fixed "now" so tests are repeatable

    // --- validation (same rules as the console version)
    CHECK(isValidSegmentId(1) && isValidSegmentId(3));
    CHECK(!isValidSegmentId(0) && !isValidSegmentId(4));
    CHECK(isValidDepth(0.0) && isValidDepth(10.0));
    CHECK(!isValidDepth(-0.01) && !isValidDepth(10.01));
    double d = 0; int n = 0;
    CHECK(parseDouble("0.45", d) && d == 0.45);
    CHECK(!parseDouble("abc", d) && !parseDouble("0.4x", d) && !parseDouble("", d));
    CHECK(parseInt("2", n) && n == 2);
    CHECK(!parseInt("2.5", n));
    CHECK(roundToCentimeter(0.456) == 0.46);

    // --- status thresholds (segment 1: alert 0.4, critical 0.8)
    freshState(dir);
    CHECK(getStatus(NO_READING, 0, T0, 0.4, 0.8) == STATUS_NO_DATA);
    CHECK(getStatus(0.39, T0, T0, 0.4, 0.8) == STATUS_SAFE);
    CHECK(getStatus(0.40, T0, T0, 0.4, 0.8) == STATUS_ALERT);
    CHECK(getStatus(0.80, T0, T0, 0.4, 0.8) == STATUS_CRITICAL);
    CHECK(getStatus(0.10, T0 - 31 * 60, T0, 0.4, 0.8) == STATUS_OUTDATED);   // old but low
    CHECK(getStatus(0.90, T0 - 31 * 60, T0, 0.4, 0.8) == STATUS_CRITICAL);   // old but high stays critical
    CHECK(getStatus(0.10, T0 + 600, T0, 0.4, 0.8) == STATUS_OUTDATED);       // clock went backwards
    CHECK(getStatusLabel(STATUS_ALERT) == "ALERT");

    // --- overall status
    int counts[STATUS_COUNT];
    countStatuses(levels, times, T0, alertL, critL, counts);
    CHECK(counts[STATUS_NO_DATA] == 3 && getOverallStatus(counts) == OVERALL_INCOMPLETE);
    CHECK(findHighestRiskIndex(levels, alertL, critL) == -1);

    // --- recording, saving, reloading
    CHECK(recordReading(1, 0.30, names, levels, times, alertL, critL, T0) == READING_OK);
    CHECK(recordReading(0, 0.30, names, levels, times, alertL, critL, T0) == READING_BAD_SEGMENT);
    CHECK(recordReading(1, 10.5, names, levels, times, alertL, critL, T0) == READING_BAD_DEPTH);
    CHECK(recordReading(1, 0.30, names, levels, times, alertL, critL, -5) == READING_BAD_CLOCK);
    CHECK(levels[0] == 0.30 && times[0] == T0);                // rejected calls changed nothing

    double l2[NUM_SEGMENTS]; time_t t2[NUM_SEGMENTS];
    CHECK(loadData(l2, t2) == LOAD_OK);
    CHECK(l2[0] == 0.30 && t2[0] == T0 && l2[1] == NO_READING);

    // --- crash-safe saving: no leftover temp file, and a failed save leaves the old file untouched
    CHECK(!ifstream((dir + "flood_data.txt.tmp").c_str()).good());
    {
        string before; { ifstream f((dir + "flood_data.txt").c_str()); getline(f, before); }
        // a folder in the way -> temp file cannot be created
        error_code ec2;
        filesystem::create_directory(filesystem::path(dir + "flood_data.txt.tmp"), ec2);
        CHECK(!ec2);
        levels[0] = 0.99;
        CHECK(!saveData(levels, times));
        string after; { ifstream f((dir + "flood_data.txt").c_str()); getline(f, after); }
        CHECK(before == after);
        filesystem::remove(filesystem::path(dir + "flood_data.txt.tmp"), ec2);
        levels[0] = 0.30;
        CHECK(saveData(levels, times));
    }

    // --- corrupted data file is refused, state stays "no reading"
    { ofstream f((dir + "flood_data.txt").c_str()); f << "0.30 123\nnot a number\n0.1 5\n"; }
    CHECK(loadData(l2, t2) == LOAD_BAD_FILE && l2[0] == NO_READING);
    remove((dir + "flood_data.txt").c_str());
    CHECK(loadData(l2, t2) == LOAD_NO_FILE);

    // --- trend + projection (two readings 10 minutes apart, +10 cm)
    freshState(dir);
    CHECK(recordReading(1, 0.30, names, levels, times, alertL, critL, T0) == READING_OK);
    CHECK(recordReading(1, 0.40, names, levels, times, alertL, critL, T0 + 600) == READING_OK);
    double rate = 0;
    CHECK(getSegmentTrend(1, levels[0], times[0], rate) == TREND_RISING);
    CHECK(rate > 0.59 && rate < 0.61);                          // 0.10 m in 1/6 h = 0.6 m/h
    CHECK(getTrendLine(1, levels[0], times[0]).find("RISING") != string::npos);
    string proj = getProjectionLine(1, levels[0], times[0], alertL[0], critL[0], T0 + 600);
    CHECK(proj.find("CRITICAL") != string::npos);               // already at alert 0.40 -> next limit is critical
    // too-close readings -> no trend
    CHECK(recordReading(1, 0.50, names, levels, times, alertL, critL, T0 + 660) == READING_OK);
    CHECK(getSegmentTrend(1, levels[0], times[0], rate) == TREND_UNKNOWN);
    // falling
    freshState(dir);
    recordReading(2, 0.50, names, levels, times, alertL, critL, T0);
    recordReading(2, 0.30, names, levels, times, alertL, critL, T0 + 1200);
    CHECK(getSegmentTrend(2, levels[1], times[1], rate) == TREND_FALLING);

    // --- suspicious change needs confirmation (>= 50 cm)
    CHECK(isSuspiciousChange(0.10, T0, 0.60, T0 + 60));
    CHECK(!isSuspiciousChange(0.10, T0, 0.59, T0 + 60));
    CHECK(!isSuspiciousChange(0.10, T0 - 3600, 0.90, T0));      // old reading -> not suspicious
    CHECK(!isSuspiciousChange(NO_READING, 0, 0.90, T0));

    // --- operator name rules
    CHECK(setOperatorName("Enforcer 07") && getOperatorName() == "Enforcer 07");
    CHECK(!setOperatorName("") && !setOperatorName("   ") && !setOperatorName("a|b"));
    CHECK(!setOperatorName(string(31, 'x')) && setOperatorName(string(30, 'x')));
    setOperatorName("Enforcer 07");

    // --- history, statistics, report
    string lines[RECENT_LOG_LIMIT];
    int cnt = getRecentHistory(lines);
    CHECK(cnt == 2 && lines[1].find("by Enforcer 07") == string::npos);   // first two logged as "unknown"
    for (int i = 0; i < 12; i++)
        recordReading(3, 0.1 + i * 0.01, names, levels, times, alertL, critL, T0 + 5000 + i * 60);
    cnt = getRecentHistory(lines);
    CHECK(cnt == RECENT_LOG_LIMIT);                                        // only the latest 10 kept
    int rc[NUM_SEGMENTS], wc[NUM_SEGMENTS]; double pk[NUM_SEGMENTS]; time_t pt[NUM_SEGMENTS];
    int total = computeStatistics(alertL, critL, rc, pk, pt, wc);
    CHECK(total == 14 && rc[1] == 2 && rc[2] == 12 && rc[0] == 0);
    CHECK(pk[1] == 0.50);
    string report = buildSituationReport(names, levels, times, alertL, critL, T0 + 6000);
    CHECK(report.find("JP LAUREL HIGHWAY FLOOD SITUATION REPORT") != string::npos);
    CHECK(report.find("Prepared by : Enforcer 07") != string::npos);
    string path = saveSituationReport(report, T0 + 6000);
    CHECK(path.find(dir) == 0 && ifstream(path.c_str()).good());

    printf("%d checks, %d failed\n", checks, failures);

    // clean up the temp folder
    filesystem::remove_all(tmp, ec);

    return failures == 0 ? 0 : 1;
}