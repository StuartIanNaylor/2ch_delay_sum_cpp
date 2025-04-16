#ifndef BEAMFORMER_DEFS_H
#define BEAMFORMER_DEFS_H

// Configuration Constants
#define SAMPLE_RATE 32000       // 32kHz sampling rate
#define BITS_PER_SAMPLE 16
#define NUM_CHANNELS 2
#define FRAME_SIZE 512        // Process audio in chunks of 512 samples
#define BUFFER_SIZE 4096      // Circular buffer size (power of 2)
#define FFT_SIZE 1024         // FFT size for frequency domain processing
#define MAX_DOA_ANGLES 181    // 0 to 180 degrees
#define DOA_ANGLE_STEP 1      // 1 degree steps
#define MAX_LOG_ENTRIES 1000  // Circular log buffer size

// Beamformer Parameters
#define MIC_DISTANCE 0.0585   // Distance between microphones in meters (58.5mm)
#define SOUND_SPEED 343.0     // Speed of sound in m/s
#define MAX_STEERING_DELAY 24 // Maximum delay in samples for steering (increased for 32kHz)

// Error recovery constants
#define MAX_XRUN_RETRIES 5    // Maximum number of xrun recovery attempts
#define WATCHDOG_TIMEOUT_MS 5000 // Watchdog timeout in milliseconds

// Logging constants
#define DEFAULT_LOGGING 0     // Default logging state (0 = off, 1 = on)

// Application state enumeration
enum AppState {
    STATE_INIT,
    STATE_RUNNING,
    STATE_ERROR,
    STATE_RECOVERY,
    STATE_TERMINATING
};

// Error types for recovery
enum ErrorType {
    ERROR_NONE,
    ERROR_ALSA_XRUN,
    ERROR_ALSA_SUSPEND,
    ERROR_PROCESSING,
    ERROR_SYSTEM
};

#endif // BEAMFORMER_DEFS_H