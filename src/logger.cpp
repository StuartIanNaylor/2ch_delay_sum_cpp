#include "logger.h"
#include <cstdio>

Logger::Logger(bool useSyslog, bool enabled) : 
    logBuffer(MAX_LOG_ENTRIES),
    nextLogIndex(0),
    logToSyslog(useSyslog),
    loggingEnabled(enabled) {
    
    if (logToSyslog) {
        openlog("beamformer", LOG_PID, LOG_USER);
    }
}

Logger::~Logger() {
    if (logToSyslog) {
        closelog();
    }
}

void Logger::setLoggingEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(logMutex);
    loggingEnabled = enabled;
    
    if (loggingEnabled) {
        log(LOG_INFO, "Logging enabled");
    }
}

void Logger::log(int level, const std::string& message) {
    std::lock_guard<std::mutex> lock(logMutex);
    
    // Skip logging if disabled, except for errors which are always logged
    if (!loggingEnabled && level != LOG_ERR) {
        return;
    }
    
    // Create log entry
    LogEntry entry;
    entry.level = level;
    entry.message = message;
    entry.timestamp = time(nullptr);
    
    // Store in circular buffer
    logBuffer[nextLogIndex] = entry;
    nextLogIndex = (nextLogIndex + 1) % MAX_LOG_ENTRIES;
    
    // Log to syslog if enabled
    if (logToSyslog) {
        syslog(level, "%s", message.c_str());
    }
    
    // Also print to stderr for debugging
    std::string levelStr;
    switch (level) {
        case LOG_ERR: levelStr = "ERROR"; break;
        case LOG_WARNING: levelStr = "WARNING"; break;
        case LOG_INFO: levelStr = "INFO"; break;
        case LOG_DEBUG: levelStr = "DEBUG"; break;
        default: levelStr = "UNKNOWN"; break;
    }
    
    fprintf(stderr, "[%s] %s\n", levelStr.c_str(), message.c_str());
}

void Logger::dumpLogs(const std::string& filename) {
    std::lock_guard<std::mutex> lock(logMutex);
    
    FILE* file = fopen(filename.c_str(), "w");
    if (!file) {
        log(LOG_ERR, "Cannot open log file: " + filename);
        return;
    }
    
    // Dump logs in chronological order
    for (size_t i = 0; i < MAX_LOG_ENTRIES; i++) {
        size_t index = (nextLogIndex + i) % MAX_LOG_ENTRIES;
        const LogEntry& entry = logBuffer[index];
        
        if (entry.timestamp == 0) {
            continue;  // Skip empty entries
        }
        
        // Format time
        char timeStr[64];
        struct tm* tm_info = localtime(&entry.timestamp);
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", tm_info);
        
        // Write log entry
        fprintf(file, "[%s] [%d] %s\n", timeStr, entry.level, entry.message.c_str());
    }
    
    fclose(file);
}

std::vector<LogEntry> Logger::getRecentLogs(int count) {
    std::lock_guard<std::mutex> lock(logMutex);
    
    std::vector<LogEntry> recent;
    
    // Calculate how many logs to return
    count = std::min(count, MAX_LOG_ENTRIES);
    
    // Get the most recent logs
    for (int i = 0; i < count; i++) {
        size_t index = (nextLogIndex - i - 1 + MAX_LOG_ENTRIES) % MAX_LOG_ENTRIES;
        const LogEntry& entry = logBuffer[index];
        
        if (entry.timestamp == 0) {
            continue;  // Skip empty entries
        }
        
        recent.push_back(entry);
    }
    
    return recent;
}