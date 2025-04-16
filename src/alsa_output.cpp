#include "alsa_output.h"
#include <chrono>
#include <algorithm>

AlsaOutput::AlsaOutput(const std::string& dev, CircularBuffer* inBuf, 
                     ErrorHandler* errHandler, Logger* log) : 
    AlsaDevice(dev, SAMPLE_RATE, 1, errHandler, log), // 1 channel for mono output
    inputBuffer(inBuf) {
}

AlsaOutput::~AlsaOutput() {
    stop();
}

bool AlsaOutput::init() {
    logger->log(LOG_INFO, "Initializing audio output with device: " + device);
    
    snd_pcm_hw_params_t *hw_params;
    snd_pcm_hw_params_alloca(&hw_params);
    
    if (!initAlsaParams(SND_PCM_STREAM_PLAYBACK, hw_params)) {
        return false;
    }
    
    // Configure software parameters for playback
    snd_pcm_sw_params_t *sw_params;
    snd_pcm_sw_params_alloca(&sw_params);
    
    int err;
    if ((err = snd_pcm_sw_params_current(handle, sw_params)) < 0) {
        logger->log(LOG_ERR, "Cannot get sw params: " + std::string(snd_strerror(err)));
        return false;
    }
    
    // Set avail min to period size to wake up when enough data can be processed
    if ((err = snd_pcm_sw_params_set_avail_min(handle, sw_params, periodSize)) < 0) {
        logger->log(LOG_ERR, "Cannot set avail min: " + std::string(snd_strerror(err)));
        return false;
    }
    
    // Start playing after enough data is written to fill one period
    if ((err = snd_pcm_sw_params_set_start_threshold(handle, sw_params, periodSize)) < 0) {
        logger->log(LOG_ERR, "Cannot set start threshold: " + std::string(snd_strerror(err)));
        return false;
    }
    
    // Allow the transfer to stop when buffer becomes empty
    if ((err = snd_pcm_sw_params_set_stop_threshold(handle, sw_params, bufferSize)) < 0) {
        logger->log(LOG_ERR, "Cannot set stop threshold: " + std::string(snd_strerror(err)));
        return false;
    }
    
    // Apply software parameters
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
    logger->log(LOG_INFO, "Audio output initialized successfully");
    return true;
}

bool AlsaOutput::start() {
    if (running) {
        return true;
    }
    
    running = true;
    deviceThread = std::thread(&AlsaOutput::outputLoop, this);
    
    logger->log(LOG_INFO, "Audio output started");
    return true;
}

void AlsaOutput::stop() {
    if (!running) {
        return;
    }
    
    running = false;
    
    if (deviceThread.joinable()) {
        deviceThread.join();
    }
    
    // Drain any remaining data
    snd_pcm_drain(handle);
    
    logger->log(LOG_INFO, "Audio output stopped");
}

void AlsaOutput::outputLoop() {
    int err;
    std::vector<int16_t> buffer(periodSize);
    
    logger->log(LOG_INFO, "Output thread started, writing " + std::to_string(periodSize) + " frames per period");
    
    // Pre-buffer some silence to prevent initial underruns
    std::vector<int16_t> silence(periodSize, 0);
    for (int i = 0; i < 2; i++) {
        snd_pcm_writei(handle, silence.data(), periodSize);
    }
    
    while (running) {
        if (state == STATE_ERROR || state == STATE_RECOVERY || state == STATE_TERMINATING) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        // Read processed samples from the input buffer
        size_t read = inputBuffer->read(buffer.data(), periodSize);
        
        if (read < periodSize) {
            if (inputBuffer->isClosed()) {
                logger->log(LOG_INFO, "Input buffer closed, exiting output loop");
                break;
            }
            
            // Not enough data, pad with silence and avoid playing partial buffers
            if (read > 0) {
                logger->log(LOG_WARNING, "Buffer underrun in output, padding with silence");
                // Fill remaining buffer with silence
                std::fill(buffer.begin() + read, buffer.end(), 0);
            } else {
                // No data at all, wait a bit and try again
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
        }
        
        // Write to audio device
        snd_pcm_sframes_t frames = snd_pcm_writei(handle, buffer.data(), periodSize);
        
        if (frames < 0) {
            logger->log(LOG_WARNING, "ALSA write error: " + std::string(snd_strerror(frames)));
            
            if (frames == -EPIPE) {  // Underrun
                logger->log(LOG_WARNING, "ALSA buffer underrun");
                
                if ((err = snd_pcm_recover(handle, frames, 1)) < 0) {
                    logger->log(LOG_ERR, "Cannot recover from underrun: " + std::string(snd_strerror(err)));
                    state = STATE_ERROR;
                    continue;
                }
            } else if (frames == -ESTRPIPE) {  // Suspended
                logger->log(LOG_WARNING, "ALSA suspended");
                
                while ((err = snd_pcm_resume(handle)) == -EAGAIN) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                
                if (err < 0) {
                    if ((err = snd_pcm_prepare(handle)) < 0) {
                        logger->log(LOG_ERR, "Cannot recover from suspend: " + std::string(snd_strerror(err)));
                        state = STATE_ERROR;
                        continue;
                    }
                }
            } else {
                // Some other error, try to recover
                if ((err = snd_pcm_recover(handle, frames, 1)) < 0) {
                    logger->log(LOG_ERR, "Cannot recover from error: " + std::string(snd_strerror(err)));
                    state = STATE_ERROR;
                    continue;
                }
            }
            
            // Retry after recovery
            frames = snd_pcm_writei(handle, buffer.data(), periodSize);
            if (frames < 0) {
                logger->log(LOG_ERR, "Failed to write after recovery: " + std::string(snd_strerror(frames)));
                state = STATE_ERROR;
                continue;
            }
        }
        
        if (frames > 0 && frames < periodSize) {
            logger->log(LOG_WARNING, "Short write: " + std::to_string(frames) + " frames of " + std::to_string(periodSize));
        } else if (frames > 0) {
            logger->log(LOG_INFO, "Wrote " + std::to_string(frames) + " frames to audio output");
        }
    }
    
    logger->log(LOG_INFO, "Output thread stopped");
}