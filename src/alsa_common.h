#ifndef ALSA_COMMON_H
#define ALSA_COMMON_H

#include <alsa/asoundlib.h>
#include <thread>
#include <atomic>
#include <string>
#include "beamformer_defs.h"
#include "error_handler.h"
#include "logger.h"

class AlsaDevice {
protected:
    snd_pcm_t *handle;
    std::string device;
    unsigned int sampleRate;
    unsigned int channels;
    snd_pcm_format_t format;
    snd_pcm_uframes_t periodSize;
    snd_pcm_uframes_t bufferSize;
    
    std::thread deviceThread;
    std::atomic<bool> running;
    std::atomic<AppState> state;
    
    ErrorHandler* errorHandler;
    Logger* logger;

    // Common ALSA error recovery
    int xrunRecovery(int err);
    
    // Common initialization steps
    bool initAlsaParams(snd_pcm_stream_t streamType, snd_pcm_hw_params_t *hw_params);
    
public:
    AlsaDevice(const std::string& dev, unsigned int rate, unsigned int chans,
               ErrorHandler* errHandler, Logger* log);
    virtual ~AlsaDevice();
    
    bool isRunning() const { return running; }
    AppState getState() const { return state; }
    void setState(AppState newState) { state = newState; }
    
    virtual bool init() = 0;
    virtual bool start() = 0;
    virtual void stop() {}  // Changed from pure virtual to virtual with empty implementation
};

#endif // ALSA_COMMON_H