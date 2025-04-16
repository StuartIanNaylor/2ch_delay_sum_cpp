#include "alsa_common.h"
#include <chrono>

AlsaDevice::AlsaDevice(const std::string& dev, unsigned int rate, unsigned int chans,
                     ErrorHandler* errHandler, Logger* log) : 
    handle(nullptr),
    device(dev),
    sampleRate(rate),
    channels(chans),
    format(SND_PCM_FORMAT_S16_LE),
    periodSize(FRAME_SIZE),
    bufferSize(FRAME_SIZE * 8),
    running(false),
    state(STATE_INIT),
    errorHandler(errHandler),
    logger(log) {
}

AlsaDevice::~AlsaDevice() {
    stop();
    if (handle) {
        snd_pcm_close(handle);
        handle = nullptr;
    }
}

int AlsaDevice::xrunRecovery(int err) {
    if (err == -EPIPE) {  // Underrun
        logger->log(LOG_WARNING, "ALSA xrun (underrun)");
        err = snd_pcm_prepare(handle);
        if (err < 0) {
            logger->log(LOG_ERR, "Can't recover from underrun: " + std::string(snd_strerror(err)));
        }
        return err;
    } else if (err == -ESTRPIPE) {  // Suspended
        logger->log(LOG_WARNING, "ALSA suspend event");
        
        // Wait until suspend flag is released
        while ((err = snd_pcm_resume(handle)) == -EAGAIN) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        if (err < 0) {
            err = snd_pcm_prepare(handle);
            if (err < 0) {
                logger->log(LOG_ERR, "Can't recover from suspend: " + std::string(snd_strerror(err)));
            }
        }
        return err;
    }
    
    return err;
}

bool AlsaDevice::initAlsaParams(snd_pcm_stream_t streamType, snd_pcm_hw_params_t *hw_params) {
    int err;
    
    // Open PCM device
    if ((err = snd_pcm_open(&handle, device.c_str(), streamType, 0)) < 0) {
        logger->log(LOG_ERR, "Cannot open audio device: " + std::string(snd_strerror(err)));
        return false;
    }
    
    // Initialize hardware parameters
    if ((err = snd_pcm_hw_params_any(handle, hw_params)) < 0) {
        logger->log(LOG_ERR, "Cannot initialize hw params: " + std::string(snd_strerror(err)));
        return false;
    }
    
    // Set access type to interleaved
    if ((err = snd_pcm_hw_params_set_access(handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        logger->log(LOG_ERR, "Cannot set access type: " + std::string(snd_strerror(err)));
        return false;
    }
    
    // Set format to S16_LE
    if ((err = snd_pcm_hw_params_set_format(handle, hw_params, format)) < 0) {
        logger->log(LOG_ERR, "Cannot set sample format: " + std::string(snd_strerror(err)));
        return false;
    }
    
    // Set sample rate
    unsigned int exactRate = sampleRate;
    int dir = 0;
    if ((err = snd_pcm_hw_params_set_rate_near(handle, hw_params, &exactRate, &dir)) < 0) {
        logger->log(LOG_ERR, "Cannot set sample rate: " + std::string(snd_strerror(err)));
        return false;
    }
    
    if (exactRate != sampleRate) {
        logger->log(LOG_WARNING, "Actual rate " + std::to_string(exactRate) + " differs from requested " + std::to_string(sampleRate));
        sampleRate = exactRate;
    }
    
    // Set number of channels
    if ((err = snd_pcm_hw_params_set_channels(handle, hw_params, channels)) < 0) {
        logger->log(LOG_ERR, "Cannot set channel count: " + std::string(snd_strerror(err)));
        return false;
    }
    
    // Set period and buffer sizes
    unsigned int periods = 4;
    if ((err = snd_pcm_hw_params_set_periods_near(handle, hw_params, &periods, &dir)) < 0) {
        logger->log(LOG_WARNING, "Cannot set periods: " + std::string(snd_strerror(err)));
    }
    
    snd_pcm_uframes_t bufSize = periodSize * 8;
    if ((err = snd_pcm_hw_params_set_buffer_size_near(handle, hw_params, &bufSize)) < 0) {
        logger->log(LOG_WARNING, "Cannot set buffer size: " + std::string(snd_strerror(err)));
    }
    
    // Apply hardware parameters
    if ((err = snd_pcm_hw_params(handle, hw_params)) < 0) {
        logger->log(LOG_ERR, "Cannot set hw params: " + std::string(snd_strerror(err)));
        return false;
    }
    
    // Get actual period size
    if ((err = snd_pcm_hw_params_get_period_size(hw_params, &periodSize, &dir)) < 0) {
        logger->log(LOG_WARNING, "Cannot get period size: " + std::string(snd_strerror(err)));
    }
    
    // Get actual buffer size
    if ((err = snd_pcm_hw_params_get_buffer_size(hw_params, &bufferSize)) < 0) {
        logger->log(LOG_WARNING, "Cannot get buffer size: " + std::string(snd_strerror(err)));
    }
    
    logger->log(LOG_INFO, "ALSA configured with: rate=" + std::to_string(sampleRate) + 
               " Hz, channels=" + std::to_string(channels) + 
               ", period=" + std::to_string(periodSize) + 
               ", buffer=" + std::to_string(bufferSize));
               
    return true;
}