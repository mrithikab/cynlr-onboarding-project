//filterblock.h
#pragma once

#include <thread>
#include <atomic>
#include <cstdint>
#include <vector>

#include "ThreadSafeQueue.h"
#include "DataGenerator.h"
#include "metrics/MetricsCollector.h"
#include "Block.h"
#include "profiler/BlockProfiler.h"

class FilterBlock : public Block {
public:
    FilterBlock(int m,
        double threshold,
        ThreadSafeQueue<DataPair>* q,
        MetricsCollector* metrics = nullptr,
        bool useFileKernel = false,
        const std::string& kernelFile = "");

    void start() override;
    void stop() override;

    bool isReady() const noexcept override {
        return ready.load(std::memory_order_acquire);
    }

    std::string name() const override { return "FilterBlock"; }
    void printStats() const override;

    bool loadKernelFromFile(const std::string& path);
    
    double testApplyFIR(const std::vector<double>& samples) {
        // Reset state
        for (int i = 0; i < 9; ++i) circ_buf[i] = 0.0;
        buf_idx = 0;
        buf_count = 0;
        
        // Feed all samples
        for (double s : samples) {
            pushSample(s);
        }
        
        // Return filtered result if we have enough samples
        if (buf_count >= 9) {
            return applyCurrentWindow();
        }
        return 0.0;
    }

    double fir_kernel[9];
    void run();
    double applyCurrentWindow() const;

    // helper routines used by the implementation
    void pushSample(double sample);
    bool processSample(double sample, uint64_t proc_start, uint64_t& out_ts);
    void flushWithZeros();

    std::thread worker;
    std::atomic<bool> running;
    std::atomic<bool> ready;

    ThreadSafeQueue<DataPair>* queue;
    MetricsCollector* metrics;

    // FIR state
    double circ_buf[9];
    int    buf_idx;
    int    buf_count;

    // Processing parameters
    double TV;
    int columns;
    int currentColumn;

    // Statistics - Queue Latency (keep separate from profiler)
    uint64_t totalPairsProcessed;
    uint64_t sum_queue_latency_ns;
    uint64_t min_queue_latency_ns;
    uint64_t max_queue_latency_ns;

    BlockProfiler profiler_;

    // Memory profiling (queue occupancy)
    uint64_t totalQueueSizeSamples;
    uint64_t minQueueSize;
    uint64_t maxQueueSize;
    uint64_t queueSizeSampleCount;
};
