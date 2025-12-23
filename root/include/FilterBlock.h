#pragma once
#include <thread>
#include <atomic>
#include <vector>
#include <cstdint>
#include "ThreadSafeQueue.h"
#include "DataGenerator.h"
#include "metrics/MetricsCollector.h"

class FilterBlock {
public:
    // MetricsCollector* is optional (can be nullptr). If provided, FilterBlock records metrics via it.
    FilterBlock(int m,
        double threshold,
        ThreadSafeQueue<DataPair>* q,
        MetricsCollector* metrics = nullptr);

    void start();
    void stop();

    // Lightweight readiness probe for a startup handshake.
    bool isReady() const noexcept { return ready.load(std::memory_order_acquire); }

private:
    void run();
    double applyFilterWindowAt(int startIndex) const;
    void printStats();

    // Threading
    std::thread worker;
    std::atomic<bool> running;
    std::atomic<bool> ready; // set when consumer thread is ready

    // Input
    ThreadSafeQueue<DataPair>* queue;

    MetricsCollector* metrics; // optional metrics sink

    // Circular buffer for last 9 samples (contiguous, cache-friendly)
    double circ_buf[9];
    int buf_idx;    // next write index (0..8)
    int buf_count;  // number of valid samples (max 9)

    double TV;                    // threshold value

    // 2D layout tracking
    int columns;                  // m
    int currentColumn;            // logical column index

    // Measurement / statistics (consumer thread only)
    uint64_t totalPairsProcessed;
    uint64_t totalOutputs;
    uint64_t sum_queue_latency_ns;
    uint64_t min_queue_latency_ns;
    uint64_t max_queue_latency_ns;
    uint64_t sum_processing_ns;
    uint64_t min_processing_ns;
    uint64_t max_processing_ns;
};