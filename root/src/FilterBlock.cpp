#include "FilterBlock.h"
#include "profiler/ModuleProfiler.h"
#include <iostream>
#include <chrono>
#include <limits>
#include <fstream>

static constexpr double KERNEL[9] = {
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

static constexpr bool verbose = false;

FilterBlock::FilterBlock(int m,
    double threshold,
    ThreadSafeQueue<DataPair>* q,
    MetricsCollector* metrics_)
    : columns(m),
    TV(threshold),
    queue(q),
    running(false),
    ready(false),
    buf_idx(0),
    buf_count(9),
    currentColumn(0),
    totalPairsProcessed(0),
    totalOutputs(0),
    sum_queue_latency_ns(0),
    min_queue_latency_ns(std::numeric_limits<uint64_t>::max()),
    max_queue_latency_ns(0),
    sum_processing_ns(0),
    min_processing_ns(std::numeric_limits<uint64_t>::max()),
    max_processing_ns(0),
    metrics(metrics_)
{
    for (int i = 0; i < 9; ++i) circ_buf[i] = 0.0;
}

void FilterBlock::start() {
    running = true;
    worker = std::thread(&FilterBlock::run, this);
}

void FilterBlock::stop() {
    running = false;
    if (queue) queue->shutdown();

    if (worker.joinable())
        worker.join();

    if (metrics) metrics->flush();

    printStats();
}

// Compute filter on 9-point window starting at startIndex
double FilterBlock::applyFilterWindowAt(int startIndex) const {
    double sum = 0.0;
    for (int i = 0; i < 9; ++i) {
        int idx = startIndex + i;
        if (idx >= 9) idx -= 9;
        sum += circ_buf[idx] * KERNEL[i];
    }
    return sum;
}

static uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

void FilterBlock::run() {
    // Mark consumer ready for handshake
    ready.store(true, std::memory_order_release);

    while (true) {
        DataPair pair;
        bool ok = queue->pop(pair);
        if (!ok) {
            // queue shutdown and empty -> post-pad with zeros to flush outputs
            for (int i = 0; i < 8; ++i) {
                circ_buf[buf_idx] = 0.0;
                buf_idx = (buf_idx + 1) % 9;
                if (buf_count < 9) ++buf_count;
                if (buf_count >= 9) {
                    int window_start = (buf_idx + 4) % 9;
                    volatile double filtered = applyFilterWindowAt(window_start);
                    (void)filtered;
                    ++totalOutputs;
                }
            }
            break;
        }

        // Treat special seq value as termination sentinel
        if (pair.seq == std::numeric_limits<uint64_t>::max()) {
            for (int i = 0; i < 8; ++i) {
                circ_buf[buf_idx] = 0.0;
                buf_idx = (buf_idx + 1) % 9;
                if (buf_count < 9) ++buf_count;
                if (buf_count >= 9) {
                    int window_start = (buf_idx + 4) % 9;
                    volatile double filtered = applyFilterWindowAt(window_start);
                    (void)filtered;
                    ++totalOutputs;
                }
            }
            break;
        }

        uint64_t pop_ts = now_ns();
        uint64_t proc_start = now_ns();

        uint64_t queue_latency = 0;
        if (pair.gen_ts_ns != 0) {
            queue_latency = proc_start > pair.gen_ts_ns ? (proc_start - pair.gen_ts_ns) : 0;
            ++totalPairsProcessed;
            sum_queue_latency_ns += queue_latency;
            if (queue_latency < min_queue_latency_ns) min_queue_latency_ns = queue_latency;
            if (queue_latency > max_queue_latency_ns) max_queue_latency_ns = queue_latency;
        }

        auto process_sample = [&](double sample, uint64_t& out_ts) -> bool {
            circ_buf[buf_idx] = sample;
            buf_idx = (buf_idx + 1) % 9;
            if (buf_count < 9) ++buf_count;

            if (buf_count >= 9) {
                ModuleProfiler::ScopedTimer tm("FilterBlock.process_sample");

                int window_start = (buf_idx + 4) % 9;
                double filtered = applyFilterWindowAt(window_start);

                out_ts = now_ns();
                int output = (filtered >= TV) ? 1 : 0;

                currentColumn = (currentColumn + 1) % columns;

                uint64_t compute_time = out_ts > proc_start ? (out_ts - proc_start) : 0;
                sum_processing_ns += compute_time;
                ++totalOutputs;
                if (compute_time < min_processing_ns) min_processing_ns = compute_time;
                if (compute_time > max_processing_ns) max_processing_ns = compute_time;
                (void)output;
                return true;
            }
            return false;
        };

        uint64_t out0_ts = 0, out1_ts = 0;
        bool produced0 = process_sample(static_cast<double>(pair.a), out0_ts);
        bool produced1 = process_sample(static_cast<double>(pair.b), out1_ts);

        if (metrics) {
            uint64_t proc0 = produced0 ? (out0_ts > proc_start ? out0_ts - proc_start : 0) : 0;
            uint64_t proc1 = produced1 ? (out1_ts > proc_start ? out1_ts - proc_start : 0) : 0;
            uint64_t inter = (produced0 && produced1) ? (out1_ts > out0_ts ? out1_ts - out0_ts : 0) : 0;
            metrics->recordPair(pair.seq, pair.gen_ts_ns, pop_ts, proc_start,
                                out0_ts, out1_ts, queue_latency, proc0, proc1, inter);
        }

        if (verbose) {
            uint64_t proc0 = produced0 ? (out0_ts > proc_start ? out0_ts - proc_start : 0) : 0;
            uint64_t proc1 = produced1 ? (out1_ts > proc_start ? out1_ts - proc_start : 0) : 0;
            uint64_t inter = (produced0 && produced1) ? (out1_ts > out0_ts ? out1_ts - out0_ts : 0) : 0;
            std::cout << pair.seq << ',' << pair.gen_ts_ns << ',' << pop_ts << ',' << proc_start
                << ',' << out0_ts << ',' << out1_ts << ','
                << queue_latency << ',' << proc0 << ',' << proc1 << ',' << inter << '\n';
        }
    }

    ready.store(false, std::memory_order_release);
}

void FilterBlock::printStats() {
    std::cout << "---- FilterBlock statistics ----\n";
    std::cout << "Pairs processed: " << totalPairsProcessed << "\n";
    std::cout << "Outputs produced: " << totalOutputs << "\n";
    if (totalPairsProcessed > 0) {
        double avg_queue = static_cast<double>(sum_queue_latency_ns) / totalPairsProcessed;
        std::cout << "Queue latency (ns): avg=" << avg_queue
            << " min=" << min_queue_latency_ns
            << " max=" << max_queue_latency_ns << "\n";
    }
    if (totalOutputs > 0) {
        double avg_proc = static_cast<double>(sum_processing_ns) / totalOutputs;
        uint64_t min_proc = (min_processing_ns == std::numeric_limits<uint64_t>::max()) ? 0 : min_processing_ns;
        std::cout << "Processing time (ns, output-based): avg=" << avg_proc
            << " min=" << min_proc
            << " max=" << max_processing_ns << "\n";
    }
    std::cout << "--------------------------------\n";
}