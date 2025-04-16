#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#include "alsa_common.h"
#include "circular_buffer.h"

class AudioCapture : public AlsaDevice {
private:
    CircularBuffer* outputBuffer;
    void captureLoop();
    
public:
    AudioCapture(const std::string& dev, CircularBuffer* outBuf, 
                ErrorHandler* errHandler, Logger* log);
    ~AudioCapture();
    
    bool init() override;
    bool start() override;
    void stop() override;
};

#endif // AUDIO_CAPTURE_H