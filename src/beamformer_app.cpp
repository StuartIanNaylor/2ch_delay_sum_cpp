#include "beamformer_app.h"
#include <cstring>
#include <csignal>
#include <chrono>
#include <thread>

// Static variables
BeamFormerApp* BeamFormerApp::instance = nullptr;
std::atomic<bool> sig_received(false);

BeamFormerApp::BeamFormerApp(const std::string& inDevice, const std::string& outDevice) : 
    inputDevice(inDevice),
    outputDevice(outDevice),
    running(false) {
    
    // Store instance for signal handler
    instance = this;
}

BeamFormerApp::~BeamFormerApp() {
    stop();
    instance = nullptr;
}

bool BeamFormerApp::init() {
    // Create logger first
    logger = std::make_unique<Logger>(true);
    
    if (!logger) {
        fprintf(stderr, "Failed to create logger\n");
        return false;
    }
    
    logger->log(LOG_INFO, "Initializing BeamFormer application");
    
    // Create error handler
    errorHandler = std::make_unique<ErrorHandler>(logger.get());
    
    if (!errorHandler) {
        logger->log(LOG_ERR, "Failed to create error handler");
        return false;
    }
    
    // Create circular buffers
    captureToBeamformer = std::make_unique<CircularBuffer>(BUFFER_SIZE);
    beamformerToOutput = std::make_unique<CircularBuffer>(BUFFER_SIZE);
    
    if (!captureToBeamformer || !beamformerToOutput) {
        logger->log(LOG_ERR, "Failed to create circular buffers");
        return false;
    }
    
    // Create components
    audioCapture = std::make_unique<AudioCapture>(inputDevice, captureToBeamformer.get(), errorHandler.get(), logger.get());
    beamformer = std::make_unique<BeamFormer>(captureToBeamformer.get(), beamformerToOutput.get(), errorHandler.get(), logger.get());
    alsaOutput = std::make_unique<AlsaOutput>(outputDevice, beamformerToOutput.get(), errorHandler.get(), logger.get());
    
    if (!audioCapture || !beamformer || !alsaOutput) {
        logger->log(LOG_ERR, "Failed to create components");
        return false;
    }
    
    // Initialize components
    if (!audioCapture->init()) {
        logger->log(LOG_ERR, "Failed to initialize audio capture");
        return false;
    }
    
    if (!beamformer->init()) {
        logger->log(LOG_ERR, "Failed to initialize beamformer");
        return false;
    }
    
    if (!alsaOutput->init()) {
        logger->log(LOG_ERR, "Failed to initialize ALSA output");
        return false;
    }
    
    // Set up signal handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigHandler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    logger->log(LOG_INFO, "BeamFormer application initialized successfully");
    return true;
}

bool BeamFormerApp::start() {
    if (running) {
        return true;
    }
    
    logger->log(LOG_INFO, "Starting BeamFormer application");
    
    // Start components in reverse order to ensure buffers are ready
    if (!alsaOutput->start()) {
        logger->log(LOG_ERR, "Failed to start ALSA output");
        return false;
    }
    
    if (!beamformer->start()) {
        logger->log(LOG_ERR, "Failed to start beamformer");
        return false;
    }
    
    if (!audioCapture->start()) {
        logger->log(LOG_ERR, "Failed to start audio capture");
        return false;
    }
    
    // Start watchdog
    if (!errorHandler->startWatchdog()) {
        logger->log(LOG_WARNING, "Failed to start watchdog");
    }
    
    running = true;
    logger->log(LOG_INFO, "BeamFormer application started");
    return true;
}

void BeamFormerApp::stop() {
    if (!running) {
        return;
    }
    
    logger->log(LOG_INFO, "Stopping BeamFormer application");
    
    // Set running to false first to prevent multiple stops
    running = false;
    
    // Close buffers first to unstick any waiting threads
    if (captureToBeamformer) captureToBeamformer->close();
    if (beamformerToOutput) beamformerToOutput->close();
    
    // Stop components in order
    if (errorHandler) errorHandler->stopWatchdog();
    
    if (audioCapture) {
        audioCapture->setState(STATE_TERMINATING);
        audioCapture->stop();
    }
    
    if (beamformer) {
        beamformer->setState(STATE_TERMINATING);
        beamformer->stop();
    }
    
    if (alsaOutput) {
        alsaOutput->setState(STATE_TERMINATING);
        alsaOutput->stop();
    }
    
    logger->log(LOG_INFO, "BeamFormer application stopped");
}

void BeamFormerApp::waitForExit() {
    logger->log(LOG_INFO, "Waiting for exit signal");
    
    while (running && !sig_received) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Ping watchdog
        if (errorHandler) {
            errorHandler->pingWatchdog();
        }
    }
}

void BeamFormerApp::sigHandler(int sig) {
    // Set the signal flag
    sig_received = true;
    
    // Only log and stop once
    static std::atomic<bool> stopping(false);
    bool expected = false;
    if (stopping.compare_exchange_strong(expected, true)) {
        if (instance) {
            instance->logger->log(LOG_INFO, "Received signal: " + std::to_string(sig));
            instance->stop();
        }
    }
}