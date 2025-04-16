#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstdint>

class CircularBuffer {
private:
    std::vector<int16_t> buffer;
    size_t readPos;
    size_t writePos;
    size_t bufferSize;
    size_t bufferMask;
    
    std::mutex mutex;
    std::condition_variable notEmpty;
    std::condition_variable notFull;
    std::atomic<bool> closed;

public:
    explicit CircularBuffer(size_t size);
    ~CircularBuffer();
    
    // Prevent copying
    CircularBuffer(const CircularBuffer&) = delete;
    CircularBuffer& operator=(const CircularBuffer&) = delete;
    
    size_t write(const int16_t* data, size_t size);
    size_t read(int16_t* data, size_t size);
    void close();
    
    size_t availableRead();
    size_t availableWrite();
    bool isClosed() const { return closed; }
};

#endif // CIRCULAR_BUFFER_H