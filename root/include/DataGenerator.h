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
int probeCSVColumns(const std::string& filePath);
// DataPair: initialize members to silence C26495 and make values deterministic.
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

    // Query whether generator thread is running (useful to detect EOF termination)
    bool isRunning() const noexcept { return running.load(std::memory_order_acquire); }

private:
    void run();

    // NOTE: CSV is streamed from file in run(); we do not keep the whole file in memory.
    ThreadSafeQueue<DataPair>* queue;
    std::thread worker;
    std::atomic<bool> running;

    int columns;                 // m
    uint64_t T_ns;               // process time
    InputMode mode;

    // CSV-related: store file path only (streamed at run-time)
    std::string csvFile;

    // Sequence counter for diagnostics
    uint64_t seqCounter = 0;
};
