#ifndef ALSA_OUTPUT_H
#define ALSA_OUTPUT_H

#include "alsa_common.h"
#include "circular_buffer.h"

class AlsaOutput : public AlsaDevice {
private:
    CircularBuffer* inputBuffer;
    void outputLoop();
    
public:
    AlsaOutput(const std::string& dev, CircularBuffer* inBuf, 
              ErrorHandler* errHandler, Logger* log);
    ~AlsaOutput();
    
    bool init() override;
    bool start() override;
    void stop() override;
};

#endif // ALSA_OUTPUT_H