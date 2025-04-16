#include "audio_capture.h"
#include <chrono>

AudioCapture::AudioCapture(const std::string& dev, CircularBuffer* outBuf, 
                         ErrorHandler* errHandler, Logger* log) : 
    AlsaDevice(dev, SAMPLE_RATE, NUM_CHANNELS, errHandler, log),
    outputBuffer(outBuf) {
}

AudioCapture::~AudioCapture() {
    stop();
}

bool AudioCapture::init() {
    logger->log(LOG_INFO, "Initializing audio capture with device: " + device);
    
    snd_pcm_hw_params_t *hw_params;
    snd_pcm_hw_params_alloca(&hw_params);
    
    if (!initAlsaParams(SND_PCM_STREAM_CAPTURE, hw_params)) {
        return false;
    }
    
    // Set up software parameters for capture
    snd_pcm_sw_params_t *sw_params;
    snd_pcm_sw_params_alloca(&sw_params);
    
    int err;
    if ((err = snd_pcm_sw_params_current(handle, sw_params)) < 0) {
        logger->log(LOG_ERR, "Cannot get sw params: " + std::string(snd_strerror(err)));
        return false;
    }
    
    if ((err = snd_pcm_sw_params_set_avail_min(handle, sw_params, periodSize)) < 0) {
        logger->log(LOG_ERR, "Cannot set avail min: " + std::string(snd_strerror(err)));
        return false;
    }
    
    if ((err = snd_pcm_sw_params_set_start_threshold(handle, sw_params, 1)) < 0) {
        logger->log(LOG_ERR, "Cannot set start threshold: " + std::string(snd_strerror(err)));
        return false;
    }
    
    if ((err = snd_pcm_sw_params(handle, sw_params)) < 0) {
        logger->log(LOG_ERR, "Cannot set sw params: " + std::string(snd_strerror(err)));
        return false;
    }
    
    // Prepare PCM for use
    if ((err = snd_pcm_prepare(handle)) < 0) {
        logger->log(LOG_ERR, "Cannot prepare audio interface: " + std::string(snd_strerror(err)));
        return false;
    }
    
    state = STATE_RUNNING;
    logger->log(LOG_INFO, "Audio capture initialized successfully");
    return true;
}

bool AudioCapture::start() {
    if (running) {
        return true;
    }
    
    running = true;
    deviceThread = std::thread(&AudioCapture::captureLoop, this);
    
    logger->log(LOG_INFO, "Audio capture started");
    return true;
}

void AudioCapture::stop() {
    if (!running) {
        return;
    }
    
    running = false;
    
    if (deviceThread.joinable()) {
        deviceThread.join();
    }
    
    logger->log(LOG_INFO, "Audio capture stopped");
}

void AudioCapture::captureLoop() {
    int err;
    std::vector<int16_t> buffer(periodSize * channels);
    
    logger->log(LOG_INFO, "Capture thread started, reading " + std::to_string(periodSize) + " frames per period");
    
    while (running) {
        if (state == STATE_ERROR || state == STATE_RECOVERY || state == STATE_TERMINATING) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        // Read audio data
        snd_pcm_sframes_t frames = snd_pcm_readi(handle, buffer.data(), periodSize);
        
        if (frames < 0) {
            logger->log(LOG_WARNING, "ALSA read error: " + std::string(snd_strerror(frames)));
            
            if (xrunRecovery(frames) < 0) {
                errorHandler->reportError(ERROR_ALSA_XRUN, "Read error: " + std::string(snd_strerror(frames)));
                state = STATE_ERROR;
                continue;
            }
            continue;
        }
        
        if (frames > 0) {
            // Check if we have actual audio data
            bool hasNonZeroSamples = false;
            int16_t maxSample = 0;
            for (int i = 0; i < std::min((int)(frames * channels), 100); i++) {
                if (buffer[i] != 0) {
                    hasNonZeroSamples = true;
                    if (std::abs(buffer[i]) > std::abs(maxSample)) {
                        maxSample = buffer[i];
                    }
                }
            }
            
            if (!hasNonZeroSamples) {
                logger->log(LOG_WARNING, "All audio samples are zero");
            } else {
                logger->log(LOG_INFO, "Captured " + std::to_string(frames) + " frames, max amplitude: " + std::to_string(maxSample));
                
                // Write captured data to output buffer
                size_t samplesToWrite = frames * channels;
                size_t written = outputBuffer->write(buffer.data(), samplesToWrite);
                
                if (written < samplesToWrite) {
                    logger->log(LOG_WARNING, "Buffer overflow, dropped " + 
                             std::to_string(samplesToWrite - written) + " samples");
                } else {
                    logger->log(LOG_INFO, "Wrote " + std::to_string(written) + " samples to output buffer");
                }
            }
        } else {
            logger->log(LOG_WARNING, "ALSA read returned " + std::to_string(frames) + " frames");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    logger->log(LOG_INFO, "Capture thread stopped");
}