#include "beamformer.h"
#include <cmath>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <thread>

// Global variables
BeamFormerApp* BeamFormerApp::instance = nullptr;
std::atomic<bool> sig_received(false);

// BeamFormer DspResources implementation
BeamFormer::DspResources::DspResources() {
    for (int c = 0; c < NUM_CHANNELS; c++) {
        fftPlanFwd[c] = nullptr;
        fftIn[c] = nullptr;
        fftOut[c] = nullptr;
        delayLinePos[c] = 0;
    }
    fftPlanBwd = nullptr;
    fftProcessed = nullptr;
}

BeamFormer::DspResources::~DspResources() {
    for (int c = 0; c < NUM_CHANNELS; c++) {
        if (fftIn[c]) fftwf_free(fftIn[c]);
        if (fftOut[c]) fftwf_free(fftOut[c]);
        if (fftPlanFwd[c]) fftwf_destroy_plan(fftPlanFwd[c]);
    }
    
    if (fftProcessed) fftwf_free(fftProcessed);
    if (fftPlanBwd) fftwf_destroy_plan(fftPlanBwd);
}

bool BeamFormer::DspResources::init() {
    // Initialize FFT
    for (int c = 0; c < NUM_CHANNELS; c++) {
        fftIn[c] = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * FFT_SIZE);
        fftOut[c] = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * FFT_SIZE);
        
        if (!fftIn[c] || !fftOut[c]) {
            return false;
        }
        
        fftPlanFwd[c] = fftwf_plan_dft_1d(FFT_SIZE, fftIn[c], fftOut[c], FFTW_FORWARD, FFTW_ESTIMATE);
        if (!fftPlanFwd[c]) {
            return false;
        }
        
        // Initialize delay lines
        delayLine[c].resize(MAX_STEERING_DELAY * 2);
        delayLinePos[c] = 0;
    }
    
    fftProcessed = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * FFT_SIZE);
    if (!fftProcessed) {
        return false;
    }
    
    fftPlanBwd = fftwf_plan_dft_1d(FFT_SIZE, fftProcessed, fftProcessed, FFTW_BACKWARD, FFTW_ESTIMATE);
    if (!fftPlanBwd) {
        return false;
    }
    
    return true;
}

// BeamFormer implementation
BeamFormer::BeamFormer(CircularBuffer* inBuf, CircularBuffer* outBuf, 
                     ErrorHandler* errHandler, Logger* log) : 
    inputBuffer(inBuf),
    outputBuffer(outBuf),
    running(false),
    state(STATE_INIT),
    currentSteeringAngle(90),  // Start with broadside (90 degrees)
    dspResources(new DspResources()),
    useNeon(false),
    errorHandler(errHandler),
    logger(log) {
    
    // Check for ARM NEON support
    #ifdef __ARM_NEON
    useNeon = true;
    logger->log(LOG_INFO, "ARM NEON instructions available");
    #else
    useNeon = false;
    logger->log(LOG_INFO, "ARM NEON instructions not available");
    #endif
    
    // Initialize delay angle calculations
    calculateDelaysForAngles();
}

BeamFormer::~BeamFormer() {
    stop();
}

bool BeamFormer::init() {
    logger->log(LOG_INFO, "Initializing beamformer");
    
    if (!dspResources->init()) {
        logger->log(LOG_ERR, "Failed to initialize DSP resources");
        return false;
    }
    
    state = STATE_RUNNING;
    logger->log(LOG_INFO, "Beamformer initialized successfully");
    return true;
}

bool BeamFormer::start() {
    if (running) {
        return true;
    }
    
    running = true;
    processingThread = std::thread(&BeamFormer::processingLoop, this);
    
    logger->log(LOG_INFO, "Beamformer processing started");
    return true;
}

void BeamFormer::stop() {
    if (!running) {
        return;
    }
    
    running = false;
    
    if (processingThread.joinable()) {
        processingThread.join();
    }
    
    logger->log(LOG_INFO, "Beamformer processing stopped");
}

void BeamFormer::calculateDelaysForAngles() {
    // Pre-calculate delay values for all possible steering angles
    for (int angle = 0; angle <= 180; angle++) {
        // Convert angle to radians (0 degrees = left, 90 degrees = center, 180 degrees = right)
        float angleRad = (angle - 90) * M_PI / 180.0f;
        
        // Calculate time delay between microphones based on angle
        // Using formula: delay = (d * sin(?)) / c
        // where d = distance between mics, ? = angle, c = speed of sound
        float delaySeconds = (MIC_DISTANCE * sin(angleRad)) / SOUND_SPEED;
        
        // Convert to samples
        float delaySamples = delaySeconds * SAMPLE_RATE;
        
        // Store the integer delay value
        delayPerAngle[angle] = (int)round(delaySamples);
        
        // Ensure within valid range
        if (delayPerAngle[angle] > MAX_STEERING_DELAY)
            delayPerAngle[angle] = MAX_STEERING_DELAY;
        if (delayPerAngle[angle] < -MAX_STEERING_DELAY)
            delayPerAngle[angle] = -MAX_STEERING_DELAY;
    }
}

void BeamFormer::processingLoop() {
    logger->log(LOG_INFO, "Processing thread started");
    
    std::vector<int16_t> inBuffer(FRAME_SIZE * NUM_CHANNELS);
    std::vector<int16_t> outBuffer(FRAME_SIZE);
    
    while (running) {
        if (state == STATE_ERROR || state == STATE_RECOVERY || state == STATE_TERMINATING) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        // Read input samples
        size_t read = inputBuffer->read(inBuffer.data(), FRAME_SIZE * NUM_CHANNELS);
        
        if (read < FRAME_SIZE * NUM_CHANNELS) {
            if (inputBuffer->isClosed()) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        
        try {
            // Estimate DOA every few frames
            static int frameCount = 0;
            if (frameCount++ % 10 == 0) {
                int angle = estimateDOA(inBuffer.data());
                if (angle >= 0 && angle <= 180) {
                    updateSteering(angle);
                }
            }
            
            // Include current TDOA angle in logging
            if (logger->isLoggingEnabled()) {
                logger->log(LOG_INFO, "Processing frame with steering angle: " + 
                           std::to_string(currentSteeringAngle) + " degrees");
            }
            
            // Process the current frame - simple passthrough for now
            for (int i = 0; i < FRAME_SIZE; i++) {
                // Basic mixing of channels
                outBuffer[i] = (inBuffer[i * NUM_CHANNELS] + inBuffer[i * NUM_CHANNELS + 1]) / 2;
            }
            
            // Write output samples
            size_t written = outputBuffer->write(outBuffer.data(), FRAME_SIZE);
            
            if (written < FRAME_SIZE) {
                logger->log(LOG_WARNING, "Output buffer overflow");
            }
        } catch (const std::exception& e) {
            logger->log(LOG_ERR, "Processing error: " + std::string(e.what()));
            errorHandler->reportError(ERROR_PROCESSING, e.what());
            state = STATE_ERROR;
        }
    }
    
    logger->log(LOG_INFO, "Processing thread stopped");
}

void BeamFormer::processFrameTimeDomain(const int16_t* input, int16_t* output) {
    // Simple implementation - will be replaced with actual beamforming
    for (int i = 0; i < FRAME_SIZE; i++) {
        output[i] = (input[i * NUM_CHANNELS] + input[i * NUM_CHANNELS + 1]) / 2;
    }
}

void BeamFormer::processFrameFrequencyDomain(const int16_t* input, int16_t* output) {
    // Simplified implementation
    processFrameTimeDomain(input, output);
}

int BeamFormer::estimateDOA(const int16_t* input) {
    // Placeholder - return center angle
    return 90;
}

void BeamFormer::updateSteering(int angle) {
    // Update current steering angle
    currentSteeringAngle = angle;
    
    // Include angle in log
    logger->log(LOG_INFO, "Steering angle updated to " + std::to_string(angle) + " degrees");
}

void BeamFormer::applyWeightsNeon(const int16_t* input, int16_t* output, int length) {
    // Simple implementation - will be replaced with NEON optimization
    for (int i = 0; i < length; i++) {
        output[i] = (input[i * NUM_CHANNELS] + input[i * NUM_CHANNELS + 1]) / 2;
    }
}

// BeamFormerApp implementation
BeamFormerApp::BeamFormerApp(const std::string& inDevice, const std::string& outDevice, bool enableLogging) : 
    inputDevice(inDevice),
    outputDevice(outDevice),
    loggingEnabled(enableLogging),
    running(false) {
    
    // Store instance for signal handler
    instance = this;
}

BeamFormerApp::~BeamFormerApp() {
    stop();
    instance = nullptr;
}

bool BeamFormerApp::init() {
    // Create logger first with specified logging state
    logger = std::make_unique<Logger>(true, loggingEnabled);
    
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