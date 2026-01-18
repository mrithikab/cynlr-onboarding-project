//filterblock.cpp
#include "FilterBlock.h"

#include "Util.h"

#include <iostream>
#include <chrono>
#include <limits>
#include <algorithm>
#include <fstream>
#include <cmath>
#include <cassert>

// ========================
// FIR configuration
// ========================

static constexpr int    TAPS = 9;
static constexpr int    CENTER = TAPS / 2;

static constexpr double KERNEL[TAPS] = {
    0.00025177,
    0.008666992,
    0.078025818,
    0.24130249,
    0.343757629,
    0.24130249,
    0.078025818,
    0.008666992,
    0.000125885
};

static bool isValidNumber(double v) {
    return std::isfinite(v) && !std::isnan(v);
}

// ========================
// Construction / lifecycle
// ========================

FilterBlock::FilterBlock(
    int m,
    double threshold,
    ThreadSafeQueue<DataPair>* q,
    MetricsCollector* metrics_,
    bool useFileKernel,
    const std::string& kernelFile)
    : worker(),
    running(false),
    ready(false),
    queue(q),
    metrics(metrics_),
    circ_buf{},
    buf_idx(0),
    buf_count(0),
    TV(threshold),
    columns(m),
    currentColumn(0),
    totalPairsProcessed(0),
    sum_queue_latency_ns(0),
    min_queue_latency_ns(std::numeric_limits<uint64_t>::max()),
    max_queue_latency_ns(0),
    profiler_("FilterBlock", 100000),
    totalQueueSizeSamples(0),
    minQueueSize(std::numeric_limits<uint64_t>::max()),
    maxQueueSize(0),
    queueSizeSampleCount(0)
{
    // Default: use built-in kernel
    for (int i = 0; i < TAPS; ++i) fir_kernel[i] = KERNEL[i];
    if (useFileKernel && !kernelFile.empty()) {
        if (!loadKernelFromFile(kernelFile)) {
            std::cerr << "[FilterBlock] Failed to load kernel from file. Using default kernel.\n";
        }
    }
}

bool FilterBlock::loadKernelFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "[FilterBlock] Kernel file not found: " << path << "\n";
        return false;
    }
    double vals[TAPS];
    int count = 0;
    while (in && count < TAPS) {
        double v;
        in >> v;
        if (in.fail()) {
            std::cerr << "[FilterBlock] Error: Non-numeric value in kernel file.\n";
            return false;
        }
        if (!isValidNumber(v)) {
            std::cerr << "[FilterBlock] Error: NaN or Inf in kernel file.\n";
            return false;
        }
        vals[count++] = v;
    }
    double dummy;
    if (in >> dummy) {
        std::cerr << "[FilterBlock] Error: More than " << TAPS << " values in kernel file.\n";
        return false;
    }
    if (count != TAPS) {
        std::cerr << "[FilterBlock] Error: Expected " << TAPS << " values, got " << count << ".\n";
        return false;
    }
    for (int i = 0; i < TAPS; ++i) fir_kernel[i] = vals[i];
    std::cout << "[FilterBlock] Loaded kernel from file: " << path << "\n";
    return true;
}

void FilterBlock::start()
{
    running = true;
    profiler_.startBlock(util::now_ns());
    worker = std::thread(&FilterBlock::run, this);
}

void FilterBlock::stop()
{
    running = false;

    if (queue)
        queue->shutdown();

    if (worker.joinable())
        worker.join();

    profiler_.stopBlock(util::now_ns());

    if (metrics)
        metrics->flush();
}

// ========================
// FIR core
// ========================

inline void FilterBlock::pushSample(double sample)
{
    circ_buf[buf_idx] = sample;
    buf_idx = (buf_idx + 1) % TAPS;

    if (buf_count < TAPS)
        ++buf_count;
}

double FilterBlock::applyCurrentWindow() const
{
    double sum = 0.0;
    int idx = buf_idx;
    for (int i = 0; i < TAPS; ++i)
    {
        sum += circ_buf[idx] * fir_kernel[i];
        if (++idx == TAPS) idx = 0;
    }
    return sum;
}

inline bool FilterBlock::processSample(double sample, uint64_t proc_start, uint64_t& out_ts)
{
    pushSample(sample);

    if (buf_count < TAPS)
        return false;

    double filtered = applyCurrentWindow();
    int output = (filtered >= TV) ? 1 : 0;
    out_ts = util::now_ns();
    (void)output;

    currentColumn = (currentColumn + 1) % columns;

    return true;
}

void FilterBlock::flushWithZeros()
{
    for (int i = 0; i < CENTER; ++i)
    {
        uint64_t dummy_ts = 0;
        processSample(0.0, util::now_ns(), dummy_ts);
    }
}

// ========================
// Worker thread
// ========================

void FilterBlock::run()
{
    ready.store(true, std::memory_order_release);

    while (true)
    {
        DataPair pair;

        while (!queue->try_pop(pair)) {
            if (queue->isShutdown()) {
                flushWithZeros();
                ready.store(false, std::memory_order_release);
                return;
            }
            util::cpu_relax();
        }

        if (pair.seq == std::numeric_limits<uint64_t>::max())
        {
            flushWithZeros();
            break;
        }

        size_t qsize = queue->size();
        totalQueueSizeSamples += qsize;
        ++queueSizeSampleCount;
        minQueueSize = std::min(minQueueSize, (uint64_t)qsize);
        maxQueueSize = std::max(maxQueueSize, (uint64_t)qsize);

        uint64_t pop_ts = util::now_ns();
        uint64_t proc_start = util::now_ns();

        uint64_t queue_latency = 0;
        if (pair.gen_ts_valid)
        {
            assert(proc_start >= pair.gen_ts_ns && "proc_start < gen_ts_ns: possible timestamp bug");
            queue_latency = proc_start > pair.gen_ts_ns
                ? proc_start - pair.gen_ts_ns
                : 0;

            ++totalPairsProcessed;
            sum_queue_latency_ns += queue_latency;
            min_queue_latency_ns = std::min(min_queue_latency_ns, queue_latency);
            max_queue_latency_ns = std::max(max_queue_latency_ns, queue_latency);  
        }

        uint64_t out0_ts = 0, out1_ts = 0;
        bool produced0 = processSample(static_cast<double>(pair.a), proc_start, out0_ts);
        bool produced1 = processSample(static_cast<double>(pair.b), proc_start, out1_ts);

        if (produced1) {
            uint64_t proc1 = out1_ts - proc_start;
            profiler_.recordSample(proc1);  
        }

        if (metrics)
        {
            uint64_t proc0 = produced0 ? (out0_ts - proc_start) : 0;
            uint64_t proc1 = produced1 ? (out1_ts - proc_start) : 0;
            uint64_t inter = (produced0 && produced1) ? (out1_ts - out0_ts) : 0;

            metrics->recordPair(
                pair.seq,
                pair.gen_ts_ns,
                pair.gen_ts_valid,
                pop_ts,
                proc_start,
                out0_ts,
                out1_ts,
                queue_latency,
                proc0,
                proc1,
                inter);
        }
    }

    ready.store(false, std::memory_order_release);
}

// ========================
// Stats
// ========================

void FilterBlock::printStats() const
{
    std::cout << "---- FilterBlock statistics ----\n";
    std::cout << "Pairs processed:  " << totalPairsProcessed << "\n";
    
    // Get profiler stats
    auto stats = profiler_.getStats();
    std::cout << "Outputs produced: " << stats.count << "\n";

    // Print queue latency (separate from profiler)
    if (totalPairsProcessed > 0)
    {
        double avg_queue =
            static_cast<double>(sum_queue_latency_ns) / totalPairsProcessed;

        std::cout << "Queue latency (ns): avg=" << avg_queue
            << " min=" << min_queue_latency_ns
            << " max=" << max_queue_latency_ns << "\n";
    }

    // Print processing time from profiler
    if (stats.count > 0)
    {
        std::cout << "Processing time (ns): avg=" << stats.avg_ns
            << " min=" << stats.min_ns
            << " max=" << stats.max_ns
            << " p50=" << stats.median_ns
            << " p95=" << stats.p95_ns
            << " p99=" << stats.p99_ns << "\n";
    }

    // Print block execution time and throughput from profiler
    if (stats.execution_time_ms > 0) {
        std::cout << "Total block execution time: " << stats.execution_time_ms << " ms\n";
        std::cout << "Throughput: " << stats.throughput_per_sec << " pairs/sec\n";
    }

    // Print queue occupancy
    std::cout << "\nMemory (Queue occupancy):\n";
    if (queueSizeSampleCount > 0) {
        double avg_queue_size = static_cast<double>(totalQueueSizeSamples) / queueSizeSampleCount;
        uint64_t min_qsize = (minQueueSize == std::numeric_limits<uint64_t>::max()) ? 0 : minQueueSize;
        
        std::cout << "  Avg queue size: " << avg_queue_size << "\n";
        std::cout << "  Min queue size: " << min_qsize << "\n";
        std::cout << "  Max queue size: " << maxQueueSize << "\n";
        
        if (queue) {
            size_t capacity = queue->capacity();
            double utilization = (avg_queue_size / capacity) * 100.0;
            std::cout << "  Queue capacity: " << capacity << "\n";
            std::cout << "  Avg utilization: " << utilization << "%\n";
        }
    }

    std::cout << "--------------------------------\n";
}
