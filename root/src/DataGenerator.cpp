//datagenerator.cpp
#include "DataGenerator.h"
#include "Util.h"

#include <random>
#include <chrono>
#include <thread>
#include <fstream>
#include "stream/CsvStreamer.h"
#include <iostream>
#include <string>
#include <algorithm>
#include <immintrin.h>
#include <limits>

// ------------------------------------------------------------
// Hybrid sleep
// ------------------------------------------------------------
static void hybrid_sleep_ns(uint64_t ns) {
    using namespace std::chrono;
    if (ns == 0) return;

    auto target = steady_clock::now() + nanoseconds(ns);

    constexpr uint64_t headroom_ns = 2000;
    if (ns > headroom_ns)
        std::this_thread::sleep_for(nanoseconds(ns - headroom_ns));

    while (steady_clock::now() < target)
        util::cpu_relax();
}

// ------------------------------------------------------------
// Backpressure-aware push helper
// ------------------------------------------------------------
bool DataGenerator::pushWithBackpressure(const DataPair& pair,
    size_t& blocked_push_count)
{
    int attempts = 0;

    while (running && !queue->try_push(pair)) {
        ++attempts;
        ++blocked_push_count;

        if (static_cast<size_t>(attempts) < backpressureSpinLimit) {
            util::cpu_relax();
        }
        else {
            queue->push(pair);
            if (queue->isShutdown()) return false;
            return running;
        }
    }
    return running;
}

// ------------------------------------------------------------
// Output interface implementation
// ------------------------------------------------------------
void DataGenerator::emit(const DataPair& pair) {
    size_t blocked_push_count = 0;
    pushWithBackpressure(pair, blocked_push_count);
    totalBlockedPushes += blocked_push_count;
}

// ------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------
DataGenerator::DataGenerator(ThreadSafeQueue<DataPair>* q,
    int m,
    uint64_t T_ns,
    InputMode mode,
    const std::string& csvFile,
    NowFn nowFn,
    SleepFn sleepFn,
    size_t spinLimit)
    : queue(q),
    columns(m),
    T_ns(T_ns),
    mode(mode),
    running(false),
    seqCounter(0),
    csvFile(csvFile),
    backpressureSpinLimit(spinLimit),
    profiler_("DataGenerator", 100000), 
    totalQueueSizeSamples(0),
    minQueueSize(std::numeric_limits<uint64_t>::max()),
    maxQueueSize(0),
    queueSizeSampleCount(0),
    totalBlockedPushes(0) 
{
    this->nowFn = nowFn ? nowFn : []() {
        return util::now_ns();
    };
    this->sleepFn = sleepFn ? sleepFn : hybrid_sleep_ns;
}

void DataGenerator::start() {
    if (running.exchange(true)) return;
    profiler_.startBlock(util::now_ns());
    worker = std::thread(&DataGenerator::run, this);
}

void DataGenerator::stop() {
    if (mode == InputMode::CSV) {
        if (worker.joinable())
            worker.join();
    } else {
        running = false;
        if (worker.joinable())
            worker.join();
    }

    profiler_.stopBlock(util::now_ns());
}

// Wait for the worker thread to finish (joins if joinable).
void DataGenerator::wait()
{
    if (worker.joinable())
        worker.join();
}

// ------------------------------------------------------------
// Main loop
// ------------------------------------------------------------
void DataGenerator::run()
{
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 255);

    int currentColumn = 0;

    CsvStreamer csvStreamer;
    if (mode == InputMode::CSV) {
        if (!csvStreamer.open(csvFile)) {
            std::cerr << "Error: Could not open CSV file: " << csvFile << "\n";
            running = false;
            return;
        }
    }

    while (running) {
        uint64_t pair_start = util::now_ns();

        DataPair pair{};

        // ------------------ produce data ------------------
        if (mode == InputMode::RANDOM) {
            pair.a = static_cast<uint8_t>(dist(rng));
            pair.b = static_cast<uint8_t>(dist(rng));
        } else {
            if (!csvStreamer.nextPair(pair.a, pair.b)) break;
        }

        pair.gen_ts_ns = nowFn();
        pair.gen_ts_valid = true;
        pair.seq = seqCounter++;

        // Sample queue size for memory profiling
        size_t qsize = queue->size();
        totalQueueSizeSamples += qsize;
        ++queueSizeSampleCount;
        minQueueSize = std::min(minQueueSize, (uint64_t)qsize);
        maxQueueSize = std::max(maxQueueSize, (uint64_t)qsize);

        emit(pair);

        uint64_t pair_time = util::now_ns() - pair_start;
        profiler_.recordSample(pair_time);

        currentColumn = (currentColumn + 2) % columns;
        sleepFn(T_ns);
    }

    // Explicit EOF shutdown
    if (mode == InputMode::CSV)
        queue->shutdown();

    running.store(false, std::memory_order_release);
}

// ------------------------------------------------------------
// Stats
// ------------------------------------------------------------
void DataGenerator::printStats() const {
    profiler_.printStats();
    
    
    std::cout << "\nMemory (Queue occupancy - producer view):\n";
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

    std::cout << "-----------------------------------\n";
}
