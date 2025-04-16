#include "beamformer.h"
#include <string>
#include <cstdio>

// Main function
int main(int argc, char* argv[]) {
    // Default values
    std::string inputDevice = "hw:1,0";  // Default ALSA input device for ReSpeaker
    std::string outputDevice = "default"; // Default ALSA output device
    bool loggingEnabled = DEFAULT_LOGGING; // Default logging state
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-i" || arg == "--input") {
            if (i + 1 < argc) {
                inputDevice = argv[++i];
            }
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) {
                outputDevice = argv[++i];
            }
        } else if (arg == "-l" || arg == "--log") {
            if (i + 1 < argc) {
                loggingEnabled = std::stoi(argv[++i]) != 0;
            }
        } else if (arg == "-h" || arg == "--help") {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  -i, --input DEVICE    ALSA input device to use (default: hw:1,0)\n");
            printf("  -o, --output DEVICE   ALSA output device to use (default: default)\n");
            printf("  -l, --log VALUE       Enable logging (0=off, 1=on, default: %d)\n", DEFAULT_LOGGING);
            printf("  -h, --help            Show this help message\n");
            return 0;
        }
    }
    
    // Create and initialize application
    BeamFormerApp app(inputDevice, outputDevice, loggingEnabled);
    
    if (!app.init()) {
        fprintf(stderr, "Failed to initialize application\n");
        return 1;
    }
    
    // Start application
    if (!app.start()) {
        fprintf(stderr, "Failed to start application\n");
        return 1;
    }
    
    // Wait for exit signal
    app.waitForExit();
    
    return 0;
}