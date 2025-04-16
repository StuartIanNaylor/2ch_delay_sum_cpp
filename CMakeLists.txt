cmake_minimum_required(VERSION 3.10)
project(pi_beamformer)

set(CMAKE_CXX_STANDARD 14)

# Global optimization level
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=armv8-a -ftree-vectorize")

# Find required packages
find_package(ALSA REQUIRED)
find_package(Threads REQUIRED)

# FFTW3 may need manual path specification on some systems
find_library(FFTW3F_LIBRARY NAMES fftw3f)

# Define all source files
set(SOURCE_FILES
    src/alsa_common.cpp
    src/alsa_output.cpp
    src/audio_capture.cpp
    src/beamformer.cpp
    src/circular_buffer.cpp
    src/error_handler.cpp
    src/logger.cpp
    src/main.cpp
)

# Add the executable
add_executable(beamformer ${SOURCE_FILES})

# Include directories
target_include_directories(beamformer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(beamformer PRIVATE ${ALSA_INCLUDE_DIRS})

# Link libraries
target_link_libraries(beamformer PRIVATE 
    ${ALSA_LIBRARIES}
    ${FFTW3F_LIBRARY}
    ${CMAKE_THREAD_LIBS_INIT}
    m
)

# Print configuration summary
message(STATUS "Configuration Summary")
message(STATUS "  ALSA Libraries: ${ALSA_LIBRARIES}")
message(STATUS "  FFTW3 Library: ${FFTW3F_LIBRARY}")