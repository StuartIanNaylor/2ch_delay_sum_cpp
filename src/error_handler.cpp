#include "error_handler.h"
#include <chrono>

ErrorHandler::ErrorHandler(Logger* log) : 
    globalState(STATE_INIT),
    lastError(ERROR_NONE),
    logger(log),
    watchdogRunning(false),
    watchdogPing(false) {
}

ErrorHandler::~ErrorHandler() {
    stopWatchdog();
}

void ErrorHandler::reportError(ErrorType err, const std::string& details) {
    std::lock_guard<std::mutex> lock(errorMutex);
    
    lastError = err;
    
    switch (err) {
        case ERROR_ALSA_XRUN:
            logger->log(LOG_ERR, "ALSA xrun error: " + details);
            break;
        case ERROR_ALSA_SUSPEND:
            logger->log(LOG_ERR, "ALSA suspend error: " + details);
            break;
        case ERROR_PROCESSING:
            logger->log(LOG_ERR, "Processing error: " + details);
            break;
        case ERROR_SYSTEM:
            logger->log(LOG_ERR, "System error: " + details);
            break;
        default:
            logger->log(LOG_ERR, "Unknown error: " + details);
            break;
    }
    
    // Set global state to error
    setGlobalState(STATE_ERROR);
    
    // Attempt to recover automatically
    if (recoverFromError()) {
        logger->log(LOG_INFO, "Recovered from error");
        setGlobalState(STATE_RUNNING);
    }
}

bool ErrorHandler::recoverFromError() {
    std::lock_guard<std::mutex> lock(errorMutex);
    
    bool recovered = false;
    
    // Set state to recovery
    setGlobalState(STATE_RECOVERY);
    
    switch (lastError) {
        case ERROR_ALSA_XRUN:
            // ALSA xrun recovery is handled in AudioCapture/AlsaOutput
            recovered = true;
            break;
        case ERROR_ALSA_SUSPEND:
            // ALSA suspend recovery is handled in AudioCapture/AlsaOutput
            recovered = true;
            break;
        case ERROR_PROCESSING:
            // Processing errors may require restarting the processing thread
            recovered = false;
            break;
        case ERROR_SYSTEM:
            // System errors may require more complex recovery
            recovered = false;
            break;
        default:
            recovered = false;
            break;
    }
    
    // If recovery failed, stay in error state
    if (!recovered) {
        setGlobalState(STATE_ERROR);
    }
    
    return recovered;
}

void ErrorHandler::setGlobalState(AppState state) {
    globalState = state;
    logger->log(LOG_INFO, "Global state changed to: " + std::to_string(state));
}

AppState ErrorHandler::getGlobalState() const {
    return globalState;
}

bool ErrorHandler::startWatchdog() {
    if (watchdogRunning) {
        return true;
    }
    
    watchdogRunning = true;
    watchdogPing = true;
    watchdogThread = std::thread(&ErrorHandler::watchdogLoop, this);
    
    logger->log(LOG_INFO, "Watchdog started");
    return true;
}

void ErrorHandler::stopWatchdog() {
    if (!watchdogRunning) {
        return;
    }
    
    watchdogRunning = false;
    
    if (watchdogThread.joinable()) {
        watchdogThread.join();
    }
    
    logger->log(LOG_INFO, "Watchdog stopped");
}

void ErrorHandler::pingWatchdog() {
    watchdogPing = true;
}

void ErrorHandler::watchdogLoop() {
    logger->log(LOG_INFO, "Watchdog thread started");
    
    while (watchdogRunning) {
        // Wait for ping timeout
        for (int i = 0; i < WATCHDOG_TIMEOUT_MS / 100 && watchdogRunning; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        if (!watchdogRunning) {
            break;
        }
        
        // Check if ping was received
        if (!watchdogPing) {
            logger->log(LOG_ERR, "Watchdog timeout - system stalled");
            reportError(ERROR_SYSTEM, "Watchdog timeout");
        }
        
        // Reset ping flag
        watchdogPing = false;
    }
    
    logger->log(LOG_INFO, "Watchdog thread stopped");
}