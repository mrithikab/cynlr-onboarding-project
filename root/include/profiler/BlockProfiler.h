#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <limits>
#include <algorithm>
#include <iostream>

// Lightweight per-block profiler (no mutexes, per-instance)
class BlockProfiler {
public:
    explicit BlockProfiler(const std::string& blockName, size_t reserveSize = 100000)
        : name_(blockName),
          blockStartTimeNs_(0),
          totalExecutionTimeNs_(0),
          totalSamples_(0),
          sumNs_(0),
          minNs_(std::numeric_limits<uint64_t>::max()),
          maxNs_(0)
    {
        samples_.reserve(reserveSize);
    }

    // Start block timer (call at block start)
    void startBlock(uint64_t timestamp) {
        blockStartTimeNs_ = timestamp;
    }

    // Stop block timer (call at block stop)
    void stopBlock(uint64_t timestamp) {
        if (blockStartTimeNs_ > 0) {
            totalExecutionTimeNs_ = timestamp - blockStartTimeNs_;
        }
    }

    // Record a single sample (per-pair, per-operation, etc.)
    void recordSample(uint64_t ns) {
        totalSamples_++;
        sumNs_ += ns;
        if (ns < minNs_) minNs_ = ns;
        if (ns > maxNs_) maxNs_ = ns;
        samples_.push_back(ns);
    }

    // Get statistics
    struct Stats {
        uint64_t count;
        uint64_t avg_ns;
        uint64_t min_ns;
        uint64_t max_ns;
        uint64_t median_ns;
        uint64_t p95_ns;
        uint64_t p99_ns;
        double throughput_per_sec;
        double execution_time_ms;
    };

    Stats getStats() const {
        Stats stats = {};
        stats.count = totalSamples_;
        stats.avg_ns = totalSamples_ ? (sumNs_ / totalSamples_) : 0;
        stats.min_ns = (minNs_ == std::numeric_limits<uint64_t>::max()) ? 0 : minNs_;
        stats.max_ns = maxNs_;
        stats.execution_time_ms = totalExecutionTimeNs_ / 1e6;
        stats.throughput_per_sec = totalExecutionTimeNs_ > 0 
            ? (totalSamples_ * 1e9) / totalExecutionTimeNs_ 
            : 0;

        if (!samples_.empty()) {
            std::vector<uint64_t> sorted = samples_;
            std::sort(sorted.begin(), sorted.end());
            size_t n = sorted.size();
            
            stats.median_ns = sorted[n / 2];
            stats.p95_ns = sorted[n * 95 / 100];
            stats.p99_ns = sorted[n * 99 / 100];
        } else {
            stats.median_ns = stats.p95_ns = stats.p99_ns = 0;
        }

        return stats;
    }

    // Print statistics to console
    void printStats() const {
        auto stats = getStats();
        
        std::cout << "---- " << name_ << " Statistics ----\n";
        std::cout << "Samples: " << stats.count << "\n";
        
        if (stats.execution_time_ms > 0) {
            std::cout << "Total execution time: " << stats.execution_time_ms << " ms\n";
            std::cout << "Throughput: " << stats.throughput_per_sec << " samples/sec\n";
        }
        
        if (stats.count > 0) {
            std::cout << "Timing (ns): avg=" << stats.avg_ns
                      << " min=" << stats.min_ns
                      << " max=" << stats.max_ns
                      << " p50=" << stats.median_ns
                      << " p95=" << stats.p95_ns
                      << " p99=" << stats.p99_ns << "\n";
        }
        
        std::cout << "--------------------------------\n";
    }

    // Clear all data (for multi-run scenarios)
    void reset() {
        samples_.clear();
        totalSamples_ = 0;
        sumNs_ = 0;
        minNs_ = std::numeric_limits<uint64_t>::max();
        maxNs_ = 0;
        blockStartTimeNs_ = 0;
        totalExecutionTimeNs_ = 0;
    }

    // Get raw samples (for custom analysis)
    const std::vector<uint64_t>& getSamples() const {
        return samples_;
    }

private:
    std::string name_;
    uint64_t blockStartTimeNs_;
    uint64_t totalExecutionTimeNs_;
    uint64_t totalSamples_;
    uint64_t sumNs_;
    uint64_t minNs_;
    uint64_t maxNs_;
    std::vector<uint64_t> samples_;
};