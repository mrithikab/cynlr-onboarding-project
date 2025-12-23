#include "DataGenerator.h"
#include <random>
#include <chrono>
#include <thread>
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <algorithm>

// Local verbose flag for diagnostic logging in this TU (keeps logging optional and non-intrusive)
static constexpr bool verbose = false;

// -----------------------------------------------------------------
// Probe CSV: returns number of columns on the first non-empty line,
// or 0 on error.
// -----------------------------------------------------------------
int probeCSVColumns(const std::string& filePath) {
    std::ifstream in(filePath);
    if (!in) return 0;

    std::string line;
    while (std::getline(in, line)) {
        // trim whitespace
        auto not_ws = [](int ch) { return !std::isspace(ch); };
        auto first = std::find_if(line.begin(), line.end(), not_ws);
        if (first == line.end()) continue; // empty line

        // count commas -> columns = commas+1
        int commas = 0;
        for (char c : line) if (c == ',') ++commas;
        return commas + 1;
    }
    return 0;
}

// --------------------------------------------------
// Hybrid sleep helper: coarse sleep_for then short spin/yield
// Targets nanosecond precision for short waits while avoiding long busy-waiting.
// --------------------------------------------------
static void hybrid_sleep_ns(uint64_t ns) {
    using namespace std::chrono;
    if (ns == 0) return;
    auto target = steady_clock::now() + nanoseconds(ns);

    // coarse threshold: leave a small headroom for the busy-wait phase
    const uint64_t headroom_ns = 2000; // 2 µs
    if (ns > headroom_ns) {
        auto coarse = nanoseconds(ns - headroom_ns);
        std::this_thread::sleep_for(coarse);
    }

    // tight loop for final headroom — yield to reduce pure spin CPU burn
    while (steady_clock::now() < target) {
        std::this_thread::yield();
    }
}

// --------------------------------------------------
// Constructor
// --------------------------------------------------
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
    // NOTE: CSV is not preloaded — streaming is done in run().
}

// --------------------------------------------------
// Start / Stop
// --------------------------------------------------
void DataGenerator::start() {
    running = true;
    worker = std::thread(&DataGenerator::run, this);
}

void DataGenerator::stop() {
    running = false;
    if (worker.joinable())
        worker.join();
}

// --------------------------------------------------
// Main generator thread (streaming CSV mode)
// - RANDOM mode unchanged.
// - CSV mode: open file, read tokens on demand, validate+clamp, form DataPair per two tokens.
// - On EOF stop and shutdown queue cleanly.
// - Drops final single leftover token (conservative policy).
// - Uses try_push+throttle to avoid blocking producer indefinitely when queue is full.
// --------------------------------------------------
void DataGenerator::run() {
    // Random generator (used only in RANDOM mode)
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 255);

    // Logical position in the 2D array (kept for conceptual tracking)
    int currentColumn = 0;

    if (mode == InputMode::CSV) {
        // Quick existence check
        std::ifstream probe(csvFile);
        if (!probe) {
            std::cerr << "Error: Could not open CSV file: " << csvFile << "\n";
            running = false;
            return;
        }
        probe.close();
    }

    // Counters / tuning for try_push throttle
    constexpr int TRY_YIELD_ATTEMPTS = 64;
    constexpr int TRY_SLEEP_ATTEMPTS = 1024;
    size_t blocked_push_count = 0;
    const uint64_t LOG_INTERVAL = 10000;

    if (mode == InputMode::RANDOM) {
        while (running) {
            DataPair pair{};
            pair.a = static_cast<uint8_t>(dist(rng));
            pair.b = static_cast<uint8_t>(dist(rng));
            pair.gen_ts_ns = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            pair.seq = seqCounter++;

            // try_push + throttle: avoid indefinite blocking inside push()
            int attempts = 0;
            while (!queue->try_push(pair)) {
                ++attempts;
                ++blocked_push_count;
                if (attempts < TRY_YIELD_ATTEMPTS) {
                    std::this_thread::yield();
                } else if (attempts < TRY_SLEEP_ATTEMPTS) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                } else {
                    // fallback to blocking push to ensure progress after waiting
                    queue->push(pair);
                    break;
                }
            }

            // optional lightweight monitoring (quiet unless verbose)
            if (verbose && (seqCounter % LOG_INTERVAL == 0)) {
                std::cout << "Producer: seq=" << seqCounter
                    << " queue_size=" << queue->size()
                    << " capacity=" << queue->capacity()
                    << " blocked_pushes=" << blocked_push_count << "\n";
            }

            // Advance logical 2D position
            currentColumn += 2;
            if (currentColumn >= columns) currentColumn = 0;

            // Enforce process time T
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
                // read next token
                if (!std::getline(in, token, ',')) {
                    // EOF reached -> set running=false so main detects completion and exit sequence proceeds
                    running = false;
                    break;
                }

                // trim whitespace
                auto trim_front = [](std::string &s) {
                    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) { return !std::isspace(ch); }));
                };
                auto trim_back = [](std::string &s) {
                    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) { return !std::isspace(ch); }).base(), s.end());
                };
                trim_front(token);
                trim_back(token);

                if (token.empty()) {
                    // treat empty as 0 (policy)
                    first_val = 0;
                } else {
                    try {
                        long v = std::stol(token);
                        // clamp to 0..255
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
                    // read next token to form pair
                    if (!std::getline(in, token, ',')) {
                        // odd number of tokens: drop the last single sample per policy
                        have_first = false;
                        // EOF: set running=false and break to let main proceed
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

                    // form pair
                    DataPair pair{};
                    pair.a = static_cast<uint8_t>(first_val);
                    pair.b = static_cast<uint8_t>(second_val);
                    pair.gen_ts_ns = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count());
                    pair.seq = seqCounter++;

                    // try_push + throttle to avoid blocking indefinitely
                    int attempts = 0;
                    while (!queue->try_push(pair)) {
                        ++attempts;
                        ++blocked_push_count;
                        if (attempts < TRY_YIELD_ATTEMPTS) {
                            std::this_thread::yield();
                        } else if (attempts < TRY_SLEEP_ATTEMPTS) {
                            std::this_thread::sleep_for(std::chrono::microseconds(1));
                        } else {
                            // If producer cannot push after many attempts, fall back to blocking push
                            queue->push(pair);
                            break;
                        }
                    }

                    // optional lightweight monitoring
                    if (verbose && (pair.seq % LOG_INTERVAL == 0)) {
                        std::cout << "CSV Producer: seq=" << pair.seq
                            << " queue_size=" << queue->size()
                            << " capacity=" << queue->capacity()
                            << " blocked_pushes=" << blocked_push_count << "\n";
                    }

                    // advance logical column
                    currentColumn += 2;
                    if (currentColumn >= columns) currentColumn = 0;

                    // respect T
                    hybrid_sleep_ns(T_ns);

                    // reset for next pair
                    have_first = false;
                }
            } // end streaming loop

            in.close();
        }

        // After finishing CSV streaming (due to EOF or error), request queue shutdown
        // so consumer unblocks and can finish gracefully.
        queue->shutdown();
    }

    // No explicit sentinel push any more; shutdown has been requested for CSV mode above.
}