#ifndef BEAMFORMER_H
#define BEAMFORMER_H

#include <fftw3.h>
#include <thread>
#include <atomic>
#include <memory>
#include <vector>
#include "beamformer_defs.h"
#include "circular_buffer.h"
#include "error_handler.h"
#include "logger.h"

class BeamFormer {
private:
    CircularBuffer* inputBuffer;
    CircularBuffer* outputBuffer;
    std::thread processingThread;
    std::atomic<bool> running;
    std::atomic<AppState> state;
    std::atomic<int> currentSteeringAngle;  // 0-180 degrees
    
    // DSP resources
    struct DspResources {
        fftwf_plan fftPlanFwd[NUM_CHANNELS];
        fftwf_plan fftPlanBwd;
        fftwf_complex *fftIn[NUM_CHANNELS];
        fftwf_complex *fftOut[NUM_CHANNELS];
        fftwf_complex *fftProcessed;
        
        // Delay line for time-domain beamforming
        std::vector<float> delayLine[NUM_CHANNELS];
        size_t delayLinePos[NUM_CHANNELS];
        
        DspResources();
        ~DspResources();
        bool init();
    };
    
    std::unique_ptr<DspResources> dspResources;
    
    // DOA estimation
    float doaScores[MAX_DOA_ANGLES];
    int delayPerAngle[MAX_DOA_ANGLES];
    
    // ARM NEON optimization flags
    bool useNeon;
    
    ErrorHandler* errorHandler;
    Logger* logger;
    
    // Processing functions
    void calculateDelaysForAngles();
    void processFrameTimeDomain(const int16_t* input, int16_t* output);
    void processFrameFrequencyDomain(const int16_t* input, int16_t* output);
    int estimateDOA(const int16_t* input);
    void updateSteering(int angle);
    void processingLoop();
    
    // NEON-optimized functions
    void applyWeightsNeon(const int16_t* input, int16_t* output, int length);
    
public:
    BeamFormer(CircularBuffer* inBuf, CircularBuffer* outBuf, 
              ErrorHandler* errHandler, Logger* log);
    ~BeamFormer();
    
    // Prevent copying
    BeamFormer(const BeamFormer&) = delete;
    BeamFormer& operator=(const BeamFormer&) = delete;
    
    bool init();
    bool start();
    void stop();
    AppState getState() const { return state; }
    void setState(AppState newState) { state = newState; }
    int getCurrentAngle() const { return currentSteeringAngle; }
};

// Main application class
class BeamFormerApp {
private:
    std::string inputDevice;
    std::string outputDevice;
    bool loggingEnabled;  // Added logging control
    
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
    BeamFormerApp(const std::string& inputDevice, const std::string& outputDevice, bool enableLogging = DEFAULT_LOGGING);
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

#endif // BEAMFORMER_H