#pragma once
#include <string>
#include <ctime>
using namespace std;




// PART 2 - CONSTANTS

const int NUM_SEGMENTS = 3;
const double MAX_DEPTH = 10.0;
const double NO_READING = -1.0;
const int OUTDATED_AFTER_MINUTES = 30;
const int RECENT_LOG_LIMIT = 10;

const int TREND_MIN_MINUTES = 5;
const int TREND_MAX_MINUTES = 120;
const int TREND_NOISE_CM = 2;
const int PROJECTION_MAX_MINUTES = 360;

const int SUSPICIOUS_CHANGE_CM = 50;
const int MAX_OPERATOR_NAME_LENGTH = 30;

const int STATUS_NO_DATA = 0;
const int STATUS_SAFE = 1;
const int STATUS_ALERT = 2;
const int STATUS_CRITICAL = 3;
const int STATUS_OUTDATED = 4;
const int STATUS_COUNT = 5;

const int OVERALL_ALL_SAFE = 0;
const int OVERALL_INCOMPLETE = 1;
const int OVERALL_ALERT = 2;
const int OVERALL_CRITICAL = 3;

const int TREND_UNKNOWN = 0;
const int TREND_STEADY = 1;
const int TREND_RISING = 2;
const int TREND_FALLING = 3;

const int NO_TARGET = -1;

const int READING_OK = 0;
const int READING_BAD_SEGMENT = 1;
const int READING_BAD_DEPTH = 2;
const int READING_BAD_CLOCK = 3;
const int READING_NOT_SAVED = 4;
const int READING_NOT_LOGGED = 5;

const int LOAD_OK = 0;
const int LOAD_NO_FILE = 1;
const int LOAD_BAD_FILE = 2;


// PART 2 - ARRAYS

void initSegments(std::string names[], double levels[], std::time_t times[],
                  double alertLimits[], double criticalLimits[]);


// PART 3 - INPUT VALIDATION

bool parseDouble(const std::string &text, double &value);
bool isValidSegmentId(int segmentId);
bool isValidDepth(double depth);
double roundToCentimeter(double depth);
bool setOperatorName(const std::string &name);
// helpers
bool parseInt(const std::string &text, int &value);
int toCentimeters(double meters);


// PART 4 - STATUS OF A SEGMENT

bool isReadingOutdated(double level, std::time_t when, std::time_t now);
int getDepthStatus(double level, double alertLimit, double criticalLimit);
int getStatus(double level, std::time_t when, std::time_t now, double alertLimit, double criticalLimit);
// helpers
bool hasReading(double level);
double getReadingAge(std::time_t when, std::time_t now);
std::string getStatusLabel(int status);
std::string getStatusMessage(int status);


// PART 5 - OVERALL SUMMARY

void countStatuses(const double levels[], const std::time_t times[], std::time_t now,
                   const double alertLimits[], const double criticalLimits[], int counts[]);
int findHighestRiskIndex(const double levels[], const double alertLimits[], const double criticalLimits[]);
int getOverallStatus(const int counts[]);
// helpers
std::string getOverallMessage(int overall);


// PART 6 - RECORDING A READING

int recordReading(int segmentId, double depth, const std::string names[], double levels[], std::time_t times[],
                  const double alertLimits[], const double criticalLimits[], std::time_t now);
bool isReadingAccepted(int resultCode);


// PART 7 - SAVING AND LOADING

bool saveData(const double levels[], const std::time_t times[]);
int loadData(double levels[], std::time_t times[]);
// helpers
std::string getDataFilePath();
std::string getLogFilePath();
std::string getHistoryFilePath();


// PART 8 - TREND AND ESTIMATE

int getSegmentTrend(int segmentId, double level, std::time_t when, double &rate);
int classifyTrend(double prevLevel, std::time_t prevTime, double level, std::time_t when, double &rate);
// helpers
std::string getTrendLine(int segmentId, double level, std::time_t when);
std::string getProjectionLine(int segmentId, double level, std::time_t when,
                              double alertLimit, double criticalLimit, std::time_t now);


// PART 9 - SUSPICIOUS CHANGE

bool isSuspiciousChange(double oldLevel, std::time_t oldTime, double newDepth, std::time_t now);


// PART 10 - HISTORY, STATISTICS, REPORT

int getRecentHistory(std::string lines[]);
int computeStatistics(const double alertLimits[], const double criticalLimits[], int readingCounts[],
                      double peakLevels[], std::time_t peakTimes[], int warningCounts[]);
std::string describeSegment(int index, const std::string names[], const double levels[], const std::time_t times[],
                            const double alertLimits[], const double criticalLimits[], std::time_t now);
std::string buildSituationReport(const std::string names[], const double levels[], const std::time_t times[],
                                 const double alertLimits[], const double criticalLimits[], std::time_t now);
std::string saveSituationReport(const std::string &reportText, std::time_t now);
// helpers
std::string intToString(int value);
std::string formatLevel(double level);
std::string formatTimestamp(std::time_t when);
std::string formatAge(std::time_t when, std::time_t now);


// PART 11 - FRONTEND (QT BRIDGE)   // Added for mobile

void setDataDirectory(const std::string &directoryWithTrailingSlash);
std::string getOperatorName();

bool appendLog(const std::string &segmentName, double depth, int status, std::time_t when);
bool appendReadingHistory(int segmentId, double depth, std::time_t when);
bool parseHistoryLine(const std::string &line, int &segmentId, double &level, std::time_t &when);
bool findLastTwoReadings(int segmentId, double &prevLevel, std::time_t &prevTime,
                         double &lastLevel, std::time_t &lastTime);
std::string formatRate(double rate);
std::string getTrendMessage(int trend, double rate);
int getNextTargetStatus(double level, double alertLimit, double criticalLimit);
int projectMinutesToLimit(double level, std::time_t when, double rate, double limit, std::time_t now);
std::string getProjectionMessage(int targetStatus, int minutes);