#pragma once
#include <atomic>
#include <thread>
#include <vector>
#include <string>
#include "ThreadSafeQueue.h"

enum class InputMode {
    RANDOM,
    CSV
};

//int probeCSVColumns(const std::string& filePath);

// DataPair: two 8-bit samples plus timestamps and sequence number.
struct DataPair {
    uint8_t a = 0;
    uint8_t b = 0;
    uint64_t gen_ts_ns = 0;
    uint64_t seq = 0;
};

class DataGenerator {
public:
    DataGenerator(ThreadSafeQueue<DataPair>* q,
        int m,
        uint64_t T_ns,
        InputMode mode,
        const std::string& csvFile = "");

    void start();
    void stop();

    bool isRunning() const noexcept { return running.load(std::memory_order_acquire); }

private:
    void run();

    ThreadSafeQueue<DataPair>* queue;
    std::thread worker;
    std::atomic<bool> running;

    int columns;
    uint64_t T_ns;
    InputMode mode;
    std::string csvFile;
    uint64_t seqCounter = 0;
};
