#include "metrics/MetricsCollector.h"
#include <fstream>
#include <mutex>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>

class FileMetricsCollector : public MetricsCollector {
public:
    explicit FileMetricsCollector(const std::string& path = "pair_metrics.csv")
        : file_path_(path)
    {
        file_.open(file_path_, std::ofstream::out | std::ofstream::trunc);
        if (file_.is_open()) {
            file_ << "seq,gen_ts_ns,gen_ts_valid,pop_ts_ns,proc_start_ns,out0_ts_ns,out1_ts_ns,"
                     "queue_latency_ns,proc0_ns,proc1_ns,inter_output_delta_ns\n";
        } else {
            std::cerr << "FileMetricsCollector: failed to open " << file_path_ << "\n";
        }
        buffer_.reserve(BUFFER_SIZE);
    }

    ~FileMetricsCollector() override {
        flush();
        if (file_.is_open()) file_.close();
    }

    void recordPair(uint64_t seq,
                    uint64_t gen_ts_ns,
                    bool gen_ts_valid,
                    uint64_t pop_ts_ns,
                    uint64_t proc_start_ns,
                    uint64_t out0_ts_ns,
                    uint64_t out1_ts_ns,
                    uint64_t queue_latency_ns,
                    uint64_t proc0_ns,
                    uint64_t proc1_ns,
                    uint64_t inter_output_delta_ns) override
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!file_.is_open()) return;

        std::ostringstream oss;
        oss << seq << ','
            << gen_ts_ns << ','
            << (gen_ts_valid ? '1' : '0') << ','
            << pop_ts_ns << ','
            << proc_start_ns << ','
            << out0_ts_ns << ','
            << out1_ts_ns << ','
            << queue_latency_ns << ','
            << proc0_ns << ','
            << proc1_ns << ','
            << inter_output_delta_ns << '\n';
        
        buffer_.push_back(oss.str());
        
        if (buffer_.size() >= BUFFER_SIZE) {
            flushBuffer();
        }
    }

    void flush() override {
        std::lock_guard<std::mutex> lk(mutex_);
        flushBuffer();
        if (file_.is_open()) file_.flush();
    }

private:
    void flushBuffer() {
        for (const auto& row : buffer_) {
            file_ << row;
        }
        buffer_.clear();
    }

    static constexpr size_t BUFFER_SIZE = 1000;
    std::string file_path_;
    std::ofstream file_;
    std::mutex mutex_;
    std::vector<std::string> buffer_;
};

// Factory helper to avoid exposing class name in header
#include "metrics/MetricsCollector.h"
MetricsCollector* CreateFileMetricsCollector(const std::string& path) {
    return new FileMetricsCollector(path);
}