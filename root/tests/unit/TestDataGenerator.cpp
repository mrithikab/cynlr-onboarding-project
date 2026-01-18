#include <iostream>
#include <vector>
#include <cstdint>
#include <limits>
#include <cmath>
#include <string>
#include <fstream>
#include <deque>
#include <thread>
#include <mutex>
#include "DataGenerator.h"

static void fail(const std::string &msg) {
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}
static void pass(const std::string &msg) {
    std::cout << "PASS: " << msg << "\n";
}

// Thread-safe mock queue for unit testing
class MockQueue : public ThreadSafeQueue<DataPair> {
public:
    mutable std::mutex m;
    std::vector<DataPair> pushed;
    bool shutdown_called = false;
    bool always_full = false;
    size_t try_push_calls = 0;
    size_t push_calls = 0;
    MockQueue() : ThreadSafeQueue<DataPair>(32) {}
    bool try_push(const DataPair& pair) {
        std::lock_guard<std::mutex> lock(m);
        ++try_push_calls;
        if (always_full) return false;
        pushed.push_back(pair);
        return true;
    }
    void push(const DataPair& pair) {
        std::lock_guard<std::mutex> lock(m);
        ++push_calls;
        pushed.push_back(pair);
    }
    size_t size() const {
        std::lock_guard<std::mutex> lock(m);
        return pushed.size();
    }
    DataPair at(size_t i) const {
        std::lock_guard<std::mutex> lock(m);
        return pushed[i];
    }
    void shutdown() { 
        shutdown_called = true; 
        std::cout << "[MockQueue] shutdown() called" << std::endl;
    }
};

// Helper: create a temp CSV file
std::string make_csv(const std::string& name, const std::string& content) {
    std::ofstream f(name);
    f << content;
    return name;
}

void testCsvNormal() {
    std::string file = make_csv("test_csv_normal.csv", "1,2,3,4,5,6");
    MockQueue queue;
    auto nowFn = []() -> uint64_t { static uint64_t t = 1000; return t += 100; };
    auto sleepFn = [](uint64_t) {};
    DataGenerator gen(&queue, 3, 42, InputMode::CSV, file, nowFn, sleepFn);
    gen.start();
    gen.stop();
    if (!queue.shutdown_called) fail("Shutdown not called");
    if (queue.size() != 3) fail("Wrong number of pairs");
    for (size_t i = 0; i < queue.size(); ++i) {
        auto p = queue.at(i);
        if (p.a != (i*2+1) || p.b != (i*2+2)) fail("CSV values mismatch at " + std::to_string(i));
        if (p.seq != i) fail("Sequence mismatch at " + std::to_string(i));
        if (!p.gen_ts_valid) fail("Timestamp validity error");
    }
    pass("CSV normal case");
}

void testCsvMalformed() {
    std::string file = make_csv("test_csv_malformed.csv", "1,2,abc,4,5,6");
    MockQueue queue;
    auto nowFn = []() -> uint64_t { static uint64_t t = 2000; return t += 100; };
    auto sleepFn = [](uint64_t) {};
    DataGenerator gen(&queue, 3, 42, InputMode::CSV, file, nowFn, sleepFn);
    gen.start();
    gen.stop();
    if (!queue.shutdown_called) fail("Shutdown not called");
    if (queue.size() != 1) fail("Malformed: should only produce 1 pair");
    pass("CSV malformed case");
}

void testCsvEmpty() {
    std::string file = make_csv("test_csv_empty.csv", "");
    MockQueue queue;
    auto nowFn = []() -> uint64_t { static uint64_t t = 3000; return t += 100; };
    auto sleepFn = [](uint64_t) {};
    DataGenerator gen(&queue, 3, 42, InputMode::CSV, file, nowFn, sleepFn);
    gen.start();
    gen.stop();
    if (!queue.shutdown_called) fail("Shutdown not called");
    if (queue.size() != 0) fail("Empty: should produce no pairs");
    pass("CSV empty case");
}

void testRandomMode() {
    MockQueue queue;
    int num_pairs = 5;
    int columns = 2;
    auto nowFn = []() -> uint64_t { static uint64_t t = 5000; return t += 100; };
    auto sleepFn = [](uint64_t) {};
    DataGenerator gen(&queue, columns, 42, InputMode::RANDOM, "", nowFn, sleepFn);
    gen.start();
    while (queue.size() < static_cast<size_t>(num_pairs)) {
        std::this_thread::yield();
    }
    gen.stop();
    if (queue.size() < static_cast<size_t>(num_pairs)) fail("Random: too few pairs");
    for (size_t i = 0; i < static_cast<size_t>(num_pairs); ++i) {
        auto p = queue.at(i);
        if (p.seq != i) fail("Random: sequence mismatch at " + std::to_string(i));
        if (!p.gen_ts_valid) fail("Random: timestamp not valid at " + std::to_string(i));
        if (p.a > 255 || p.b > 255) fail("Random: value out of range at " + std::to_string(i));
    }
    pass("Random mode case");
}

void testBackpressureFallback() {
    struct BPQueue : public MockQueue {
        int fail_count = 3;
        bool try_push(const DataPair& pair) {
            std::lock_guard<std::mutex> lock(m);
            ++try_push_calls;
            if (fail_count-- > 0) return false;
            pushed.push_back(pair);
            return true;
        }
        void push(const DataPair& pair) {
            std::lock_guard<std::mutex> lock(m);
            ++push_calls;
            pushed.push_back(pair);
        }
    } queue;
    auto nowFn = []() -> uint64_t { static uint64_t t = 6000; return t += 100; };
    auto sleepFn = [](uint64_t) {};
    // Pass a small spinLimit (3) here so the test triggers the blocking fallback quickly.
    DataGenerator gen(&queue, 2, 42, InputMode::RANDOM, "", nowFn, sleepFn, 3);
    gen.start();
    while (queue.size() < 1) {
        std::this_thread::yield();
    }
    gen.stop();
    if (queue.try_push_calls < 3) fail("Backpressure: try_push not called enough");
    if (queue.push_calls == 0) fail("Backpressure: push() not called");
    pass("Backpressure fallback case");
}

int main() {
    std::cout << "\nRunning DataGenerator unit tests...\n";
    testCsvNormal();
    testCsvMalformed();
    testCsvEmpty();
    testRandomMode();
    testBackpressureFallback();
    std::cout << "All DataGenerator unit tests passed.\n";
    return 0;
}


