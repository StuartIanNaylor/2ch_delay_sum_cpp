#ifndef ERROR_HANDLER_H
#define ERROR_HANDLER_H

#include <atomic>
#include <mutex>
#include <thread>
#include "beamformer_defs.h"
#include "logger.h"

class ErrorHandler {
private:
    std::atomic<AppState> globalState;
    std::atomic<ErrorType> lastError;
    std::mutex errorMutex;
    Logger* logger;
    
    // Watchdog related
    std::thread watchdogThread;
    std::atomic<bool> watchdogRunning;
    std::atomic<bool> watchdogPing;
    
    void watchdogLoop();
    
public:
    explicit ErrorHandler(Logger* log);
    ~ErrorHandler();
    
    // Prevent copying
    ErrorHandler(const ErrorHandler&) = delete;
    ErrorHandler& operator=(const ErrorHandler&) = delete;
    
    void reportError(ErrorType err, const std::string& details);
    bool recoverFromError();
    void setGlobalState(AppState state);
    AppState getGlobalState() const;
    
    bool startWatchdog();
    void stopWatchdog();
    void pingWatchdog();
};

#endif // ERROR_HANDLER_H