#pragma once

#include <atomic>
#include <thread>
#include <vector>
#include <string>
#include <functional>
#include <cstdint>

#include "ThreadSafeQueue.h"
#include "Block.h"
#include "profiler/BlockProfiler.h"

using NowFn   = uint64_t (*)();
using SleepFn = void (*)(uint64_t);

enum class InputMode {
    RANDOM,
    CSV
};


struct DataPair {
    uint8_t a = 0;
    uint8_t b = 0;
    uint64_t gen_ts_ns = 0;
    bool gen_ts_valid = false;
    uint64_t seq = 0;
};


class DataGenerator : public Block {
public:
    DataGenerator(ThreadSafeQueue<DataPair>* q,
        int m,
        uint64_t T_ns,
        InputMode mode,
        const std::string& csvFile = "",
        NowFn nowFn = nullptr,
        SleepFn sleepFn = nullptr,
        size_t spinLimit = 50000);

    // Block interface implementation
    void start() override;
    void stop() override;
    bool isReady() const override { return running.load(std::memory_order_acquire); }
    void printStats() const override;
    std::string name() const override { return "DataGenerator"; }
    
    // Output interface: emit pairs to queue
    void emit(const DataPair& pair) override;

    // Existing public API (unchanged)
    bool isRunning() const noexcept { return running.load(std::memory_order_acquire); }

private:
    NowFn   nowFn;
    SleepFn sleepFn;
    void run();
    bool pushWithBackpressure(const DataPair& pair, size_t& blocked_push_count);

    ThreadSafeQueue<DataPair>* queue;
    std::thread worker;
    std::atomic<bool> running;

    int columns;
    uint64_t T_ns;
    InputMode mode;
    std::string csvFile;
    uint64_t seqCounter;

    size_t backpressureSpinLimit;

    // Profiling
    BlockProfiler profiler_;
    
    // Memory profiling (queue occupancy from producer side)
    uint64_t totalQueueSizeSamples;
    uint64_t minQueueSize;
    uint64_t maxQueueSize;
    uint64_t queueSizeSampleCount;
    uint64_t totalBlockedPushes; 
};
