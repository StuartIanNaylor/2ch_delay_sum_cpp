#ifndef BEAMFORMER_APP_H
#define BEAMFORMER_APP_H

#include <string>
#include <memory>
#include <atomic>
#include "beamformer_defs.h"
#include "beamformer.h"
#include "audio_capture.h"
#include "alsa_output.h"
#include "error_handler.h"
#include "logger.h"

class BeamFormerApp {
private:
    std::string inputDevice;
    std::string outputDevice;
    
    std::unique_ptr<CircularBuffer> captureToBeamformer;
    std::unique_ptr<CircularBuffer> beamformerToOutput;
    
    std::unique_ptr<AudioCapture> audioCapture;
    std::unique_ptr<BeamFormer> beamformer;
    std::unique_ptr<AlsaOutput> alsaOutput;
    std::unique_ptr<ErrorHandler> errorHandler;
    std::unique_ptr<Logger> logger;
    
    std::atomic<bool> running;
    
    // Signal handler for clean shutdown
    static void sigHandler(int sig);
    static BeamFormerApp* instance;
    
public:
    BeamFormerApp(const std::string& inputDevice, const std::string& outputDevice);
    ~BeamFormerApp();
    
    // Prevent copying
    BeamFormerApp(const BeamFormerApp&) = delete;
    BeamFormerApp& operator=(const BeamFormerApp&) = delete;
    
    bool init();
    bool start();
    void stop();
    void waitForExit();
    
    static BeamFormerApp* getInstance() { return instance; }
};

// Global signal flag
extern std::atomic<bool> sig_received;

#endif // BEAMFORMER_APP_H