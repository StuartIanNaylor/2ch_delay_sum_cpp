#ifndef LOGGER_H
#define LOGGER_H

#include <vector>
#include <string>
#include <mutex>
#include <ctime>
#include <syslog.h>
#include "beamformer_defs.h"

// Log entry structure
struct LogEntry {
    int level;
    std::string message;
    time_t timestamp;
};

// Logger class with circular buffer
class Logger {
private:
    std::vector<LogEntry> logBuffer;
    size_t nextLogIndex;
    std::mutex logMutex;
    bool logToSyslog;
    bool loggingEnabled;  // Added flag to control logging
    
public:
    explicit Logger(bool useSyslog = true, bool enabled = DEFAULT_LOGGING);
    ~Logger();
    
    // Prevent copying
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    void setLoggingEnabled(bool enabled);
    bool isLoggingEnabled() const { return loggingEnabled; }
    
    void log(int level, const std::string& message);
    void dumpLogs(const std::string& filename);
    std::vector<LogEntry> getRecentLogs(int count = 50);
};

#endif // LOGGER_H