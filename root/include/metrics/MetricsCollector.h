#pragma once
#include <cstdint>

// Small metrics / logging abstraction for FilterBlock.
// Implementations can write to file, to in-memory buffer, or be a no-op.
class MetricsCollector {
public:
    virtual ~MetricsCollector() = default;

    // Record one pair's timestamps and derived metrics.
    // Added gen_ts_valid to explicitly indicate whether gen_ts_ns is set.
    virtual void recordPair(uint64_t seq,
                            uint64_t gen_ts_ns,
                            bool gen_ts_valid,
                            uint64_t pop_ts_ns,
                            uint64_t proc_start_ns,
                            uint64_t out0_ts_ns,
                            uint64_t out1_ts_ns,
                            uint64_t queue_latency_ns,
                            uint64_t proc0_ns,
                            uint64_t proc1_ns,
                            uint64_t inter_output_delta_ns) = 0;

    virtual void flush() = 0;
};