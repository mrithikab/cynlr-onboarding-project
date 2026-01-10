#include "DataGenerator.h"
#include "profiler/ModuleProfiler.h"
#include <random>
#include <chrono>
#include <thread>
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <algorithm>

static constexpr bool verbose = false;

// Hybrid sleep helper: coarse sleep_for then short spin/yield for final headroom
static void hybrid_sleep_ns(uint64_t ns) {
    using namespace std::chrono;
    if (ns == 0) return;
    auto target = steady_clock::now() + nanoseconds(ns);

    const uint64_t headroom_ns = 2000; // 2 µs
    if (ns > headroom_ns) {
        auto coarse = nanoseconds(ns - headroom_ns);
        std::this_thread::sleep_for(coarse);
    }

    while (steady_clock::now() < target) {
        std::this_thread::yield();
    }
}

DataGenerator::DataGenerator(ThreadSafeQueue<DataPair>* q,
    int m,
    uint64_t T_ns,
    InputMode mode,
    const std::string& csvFile)
    : queue(q),
    columns(m),
    T_ns(T_ns),
    mode(mode),
    running(false),
    seqCounter(0),
    csvFile(csvFile)
{
}

void DataGenerator::start() {
    running = true;
    worker = std::thread(&DataGenerator::run, this);
}

void DataGenerator::stop() {
    running = false;
    if (worker.joinable())
        worker.join();
}

void DataGenerator::run() {
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 255);

    int currentColumn = 0;

    if (mode == InputMode::CSV) {
        std::ifstream probe(csvFile);
        if (!probe) {
            std::cerr << "Error: Could not open CSV file: " << csvFile << "\n";
            running = false;
            return;
        }
        probe.close();
    }

    constexpr int TRY_YIELD_ATTEMPTS = 64;
    constexpr int TRY_SLEEP_ATTEMPTS = 1024;
    size_t blocked_push_count = 0;
    const uint64_t LOG_INTERVAL = 10000;

    if (mode == InputMode::RANDOM) {
        while (running) {
            ModuleProfiler::ScopedTimer tm("DataGenerator.produce");

            DataPair pair{};
            pair.a = static_cast<uint8_t>(dist(rng));
            pair.b = static_cast<uint8_t>(dist(rng));
            pair.gen_ts_ns = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            pair.gen_ts_valid = true; // NEW: mark timestamp as valid
            pair.seq = seqCounter++;

            int attempts = 0;
            while (!queue->try_push(pair)) {
                ++attempts;
                ++blocked_push_count;
                if (attempts < TRY_YIELD_ATTEMPTS) {
                    std::this_thread::yield();
                } else if (attempts < TRY_SLEEP_ATTEMPTS) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                } else {
                    queue->push(pair);
                    break;
                }
            }

            if (blocked_push_count) {
                ModuleProfiler::Instance().recordCounter("DataGenerator.blocked_push_count", blocked_push_count);
            }

            if (verbose && (seqCounter % LOG_INTERVAL == 0)) {
                std::cout << "Producer: seq=" << seqCounter
                    << " queue_size=" << queue->size()
                    << " capacity=" << queue->capacity()
                    << " blocked_pushes=" << blocked_push_count << "\n";
            }

            currentColumn += 2;
            if (currentColumn >= columns) currentColumn = 0;

            hybrid_sleep_ns(T_ns);
        }
    }
    else if (mode == InputMode::CSV) {
        std::ifstream in(csvFile);
        if (!in) {
            std::cerr << "Error: Could not open CSV file: " << csvFile << "\n";
            running = false;
        } else {
            std::string token;
            bool have_first = false;
            int first_val = 0;

            while (running) {
                if (!std::getline(in, token, ',')) {
                    running = false;
                    break;
                }

                auto trim_front = [](std::string &s) {
                    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) { return !std::isspace(ch); }));
                };
                auto trim_back = [](std::string &s) {
                    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) { return !std::isspace(ch); }).base(), s.end());
                };
                trim_front(token);
                trim_back(token);

                if (token.empty()) {
                    first_val = 0;
                } else {
                    try {
                        long v = std::stol(token);
                        if (v < 0) v = 0;
                        if (v > 255) v = 255;
                        first_val = static_cast<int>(v);
                    } catch (const std::exception &e) {
                        std::cerr << "CSV parse error at token '" << token << "': " << e.what() << ". Stopping.\n";
                        running = false;
                        break;
                    }
                }

                if (!have_first) {
                    have_first = true;
                    if (!std::getline(in, token, ',')) {
                        have_first = false;
                        running = false;
                        break;
                    }
                    trim_front(token); trim_back(token);
                    int second_val = 0;
                    if (token.empty()) {
                        second_val = 0;
                    } else {
                        try {
                            long v = std::stol(token);
                            if (v < 0) v = 0;
                            if (v > 255) v = 255;
                            second_val = static_cast<int>(v);
                        } catch (const std::exception &e) {
                            std::cerr << "CSV parse error at token '" << token << "': " << e.what() << ". Stopping.\n";
                            running = false;
                            break;
                        }
                    }

                    DataPair pair{};

                    {
                        ModuleProfiler::ScopedTimer tm("DataGenerator.produce");
                        pair.a = static_cast<uint8_t>(first_val);
                        pair.b = static_cast<uint8_t>(second_val);
                        pair.gen_ts_ns = static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch()).count());
                        pair.gen_ts_valid = true; // NEW: mark timestamp as valid
                        pair.seq = seqCounter++;

                        int attempts = 0;
                        while (!queue->try_push(pair)) {
                            ++attempts;
                            ++blocked_push_count;
                            if (attempts < TRY_YIELD_ATTEMPTS) {
                                std::this_thread::yield();
                            } else if (attempts < TRY_SLEEP_ATTEMPTS) {
                                std::this_thread::sleep_for(std::chrono::microseconds(1));
                            } else {
                                queue->push(pair);
                                break;
                            }
                        }

                        if (blocked_push_count) {
                            ModuleProfiler::Instance().recordCounter("DataGenerator.blocked_push_count", blocked_push_count);
                        }
                    }

                    if (verbose && (pair.seq % LOG_INTERVAL == 0)) {
                        std::cout << "CSV Producer: seq=" << pair.seq
                            << " queue_size=" << queue->size()
                            << " capacity=" << queue->capacity()
                            << " blocked_pushes=" << blocked_push_count << "\n";
                    }

                    currentColumn += 2;
                    if (currentColumn >= columns) currentColumn = 0;

                    hybrid_sleep_ns(T_ns);

                    have_first = false;
                }
            } // end streaming loop

            in.close();
        }
    }

    if (!running && mode == InputMode::CSV) {
        queue->shutdown();
    }
}