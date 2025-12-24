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
    FilterBlock(int m,
        double threshold,
        ThreadSafeQueue<DataPair>* q,
        MetricsCollector* metrics = nullptr);

    void start();
    void stop();

    bool isReady() const noexcept { return ready.load(std::memory_order_acquire); }

private:
    void run();
    double applyFilterWindowAt(int startIndex) const;
    void printStats();

    std::thread worker;
    std::atomic<bool> running;
    std::atomic<bool> ready;

    ThreadSafeQueue<DataPair>* queue;
    MetricsCollector* metrics;

    double circ_buf[9];
    int buf_idx;
    int buf_count;

    double TV;
    int columns;
    int currentColumn;

    uint64_t totalPairsProcessed;
    uint64_t totalOutputs;
    uint64_t sum_queue_latency_ns;
    uint64_t min_queue_latency_ns;
    uint64_t max_queue_latency_ns;
    uint64_t sum_processing_ns;
    uint64_t min_processing_ns;
    uint64_t max_processing_ns;
};