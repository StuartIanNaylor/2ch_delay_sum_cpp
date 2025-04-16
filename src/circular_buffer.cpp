#include "circular_buffer.h"
#include <stdexcept>
#include <algorithm>

CircularBuffer::CircularBuffer(size_t size) : 
    readPos(0),
    writePos(0),
    closed(false) {
    
    // Ensure buffer size is a power of 2
    if ((size & (size - 1)) != 0) {
        throw std::invalid_argument("Buffer size must be a power of 2");
    }
    
    bufferSize = size;
    bufferMask = size - 1;
    buffer.resize(size);
}

CircularBuffer::~CircularBuffer() {
    close();
}

size_t CircularBuffer::write(const int16_t* data, size_t size) {
    if (closed) return 0;
    
    std::unique_lock<std::mutex> lock(mutex);
    
    size_t written = 0;
    while (written < size && !closed) {
        size_t available = availableWrite();
        
        if (available == 0) {
            // Buffer is full, wait for space
            auto status = notFull.wait_for(lock, std::chrono::milliseconds(100));
            if (closed) break; // Check closed flag after wait
            continue;
        }
        
        size_t toWrite = std::min(available, size - written);
        for (size_t i = 0; i < toWrite; i++) {
            buffer[(writePos + i) & bufferMask] = data[written + i];
        }
        
        writePos = (writePos + toWrite) & bufferMask;
        written += toWrite;
        
        // Notify readers that data is available
        notEmpty.notify_one();
    }
    
    return written;
}

size_t CircularBuffer::read(int16_t* data, size_t size) {
    std::unique_lock<std::mutex> lock(mutex);
    
    size_t read = 0;
    while (read < size) {
        size_t available = availableRead();
        
        if (available == 0) {
            if (closed) break;
            
            // Buffer is empty, wait for data
            auto status = notEmpty.wait_for(lock, std::chrono::milliseconds(100));
            if (closed) break; // Check closed flag after wait
            continue;
        }
        
        size_t toRead = std::min(available, size - read);
        for (size_t i = 0; i < toRead; i++) {
            data[read + i] = buffer[(readPos + i) & bufferMask];
        }
        
        readPos = (readPos + toRead) & bufferMask;
        read += toRead;
        
        // Notify writers that space is available
        notFull.notify_one();
    }
    
    return read;
}

void CircularBuffer::close() {
    std::unique_lock<std::mutex> lock(mutex);
    closed = true;
    // Signal both conditions to wake up any waiting threads
    notEmpty.notify_all();
    notFull.notify_all();
}

size_t CircularBuffer::availableRead() {
    return (writePos - readPos) & bufferMask;
}

size_t CircularBuffer::availableWrite() {
    return bufferSize - 1 - availableRead();
}