//integration test (robust: close file before start + timed drain)
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <chrono>
#include <thread>
#include "DataGenerator.h"
#include "ThreadSafeQueue.h"

static void fail(const std::string &msg) {
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}
static void pass(const std::string &msg) {
    std::cout << "PASS: " << msg << "\n";
}

static constexpr auto DEFAULT_TIMEOUT_MS = 2000;

static void drainWithTimeout(ThreadSafeQueue<DataPair>& queue,
                            DataGenerator& gen,
                            std::vector<std::pair<int,int>>& out,
                            std::chrono::milliseconds timeout = std::chrono::milliseconds(DEFAULT_TIMEOUT_MS))
{
    using namespace std::chrono;
    auto deadline = steady_clock::now() + timeout;
    while (steady_clock::now() < deadline) {
        DataPair pair;
        if (queue.try_pop(pair)) {
            // sentinel support
            if (pair.seq == std::numeric_limits<uint64_t>::max()) break;
            out.emplace_back(static_cast<int>(pair.a), static_cast<int>(pair.b));
            continue;
        }
        if (!gen.isRunning() && queue.size() == 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void testDataGeneratorCsv() {
    // Test 1: normal CSV (minimal integration check)
    {
        const std::string path = "test_data_generator.csv";
        {
            std::ofstream f(path);
            f << "1,2,3,4,5,6";
            f.close(); // <- ensure the writer releases the file before reader opens it
        }

        ThreadSafeQueue<DataPair> queue(8);
        DataGenerator gen(&queue, /*m*/4, /*T_ns*/0, InputMode::CSV, path);
        gen.start();

        std::vector<std::pair<int,int>> consumed;
        drainWithTimeout(queue, gen, consumed);

        gen.stop();

        if (consumed.size() != 3) fail("DataGenerator produced wrong number of pairs: expected 3 got " + std::to_string(consumed.size()));
        if (consumed.empty() || consumed[0] != std::make_pair(1,2)) fail("DataGenerator first pair mismatch");
        pass("DataGenerator CSV streaming integration (minimal)");
    }

    // Test 2: malformed CSV (minimal)
    {
        const std::string path = "test_data_generator_malformed.csv";
        {
            std::ofstream f(path);
            f << "1,2,abc,4";
            f.close();
        }

        ThreadSafeQueue<DataPair> queue(8);
        DataGenerator gen(&queue, /*m*/2, /*T_ns*/0, InputMode::CSV, path);
        gen.start();

        std::vector<std::pair<int,int>> consumed;
        drainWithTimeout(queue, gen, consumed);

        gen.stop();

        if (consumed.empty()) fail("DataGenerator malformed CSV: expected at least one pair");
        pass("DataGenerator malformed CSV (minimal)");
    }

    // Test 3: EOF shutdown (empty file)
    {
        const std::string path = "test_data_generator_empty.csv";
        {
            std::ofstream f(path); // empty file
            f.close();
        }

        ThreadSafeQueue<DataPair> queue(8);
        DataGenerator gen(&queue, /*m*/2, /*T_ns*/0, InputMode::CSV, path);
        gen.start();

        std::vector<std::pair<int,int>> consumed;
        drainWithTimeout(queue, gen, consumed);

        gen.stop();

        if (!consumed.empty()) fail("DataGenerator EOF shutdown: expected 0 pairs");
        pass("DataGenerator EOF shutdown (minimal)");
    }
}

int main() {
    std::cout << "\nRunning DataGenerator integration tests...\n";
    testDataGeneratorCsv();
    std::cout << "All DataGenerator integration tests passed.\n";
    return 0;
}
