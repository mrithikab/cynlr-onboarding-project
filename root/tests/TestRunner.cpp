cpp root\tests\TestRunner.cpp
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <cstdlib>

#include "stream/CsvStreamer.h"
#include "DataGenerator.h"
#include "ThreadSafeQueue.h"

// Simple test helpers
static void fail(const std::string &msg) {
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

static void pass(const std::string &msg) {
    std::cout << "PASS: " << msg << "\n";
}

// Test CsvStreamer: various tokens, empty tokens -> 0, clamping, parse error stops stream
void testCsvStreamer() {
    const std::string path = "test_csv_streamer.csv";
    // tokens: (10,20), ( ,300) -> (0,255), (-5,255) then malformed produces stop before final pair
    {
        std::ofstream f(path);
        f << "10,20, ,300,-5,abc";
    }

    CsvStreamer s;
    if (!s.open(path)) fail("CsvStreamer.open failed to open " + path);

    std::vector<std::pair<int,int>> got;
    uint8_t a=0,b=0;
    while (s.nextPair(a,b)) {
        got.emplace_back(static_cast<int>(a), static_cast<int>(b));
    }
    s.close();

    // Expected: (10,20), (0,255) because -5,abc -> parse error on 'abc' prevents third pair
    if (got.size() != 2) fail("CsvStreamer produced wrong number of pairs: expected 2 got " + std::to_string(got.size()));
    if (got[0].first != 10 || got[0].second != 20) fail("CsvStreamer pair0 mismatch");
    if (got[1].first != 0 || got[1].second != 255) fail("CsvStreamer pair1 mismatch");

    pass("CsvStreamer basic parsing and clamping");
}

// Test DataGenerator CSV streaming integration with ThreadSafeQueue
void testDataGeneratorCsv() {
    const std::string path = "test_data_generator.csv";
    // create 3 pairs: (1,2),(3,4),(5,6)
    {
        std::ofstream f(path);
        f << "1,2,3,4,5,6";
    }

    // capacity: small but >= pairs to avoid blocking fallback in short test
    ThreadSafeQueue<DataPair> queue(8);
    DataGenerator gen(&queue, /*m*/4, /*T_ns*/0, InputMode::CSV, path);

    gen.start();

    // Consume until queue shutdown (DataGenerator will call queue->shutdown() on EOF)
    std::vector<std::pair<int,int>> consumed;
    while (true) {
        DataPair pair;
        bool ok = queue.pop(pair); // blocking pop; returns false on shutdown+empty
        if (!ok) break;
        // sentinel handling if present (should not be used after shutdown API)
        if (pair.seq == std::numeric_limits<uint64_t>::max()) break;
        consumed.emplace_back(static_cast<int>(pair.a), static_cast<int>(pair.b));
    }

    // Ensure generator thread finished
    gen.stop();

    if (consumed.size() != 3) fail("DataGenerator produced wrong number of pairs: expected 3 got " + std::to_string(consumed.size()));
    if (consumed[0] != std::make_pair(1,2)) fail("DataGenerator pair0 mismatch");
    if (consumed[1] != std::make_pair(3,4)) fail("DataGenerator pair1 mismatch");
    if (consumed[2] != std::make_pair(5,6)) fail("DataGenerator pair2 mismatch");

    pass("DataGenerator CSV streaming integration");
}

int main() {
    std::cout << "Running unit tests...\n";
    testCsvStreamer();
    testDataGeneratorCsv();
    std::cout << "All tests passed.\n";
    return 0;
}