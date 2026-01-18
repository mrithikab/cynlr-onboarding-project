#include <iostream>
#include "ThreadSafeQueue.h"
#include "DataGenerator.h"
#include "FilterBlock.h"
#include "stream/CsvStreamer.h"
#include "metrics/Collectors.h"
#include "Pipeline.h"
#include "Config.h"
#include <direct.h>
#include <limits.h>
#include <string>
#include <cstdint>
#include <algorithm>

static void printUsage()
{
    std::cout
        << "Usage:\n"
        << "  --mode=random|csv\n"
        << "  --threshold=<number>\n"
        << "  --T_ns=<uint64>\n"
        << "  --columns=<int>\n"
        << "  --filter=default|file\n"
        << "  --stats | --stats=on|1|true\n"
        << "  --csv=<path>\n"
        << "  --filterfile=<path>\n"
        << "  --quiet (suppress output)\n"
        << "  --help\n";
}

static bool parseArgs(int argc, char** argv, Config& config)
{
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);

        if (arg == "--help" || arg == "-h") {
            printUsage();
            return false;
        }

        auto hasPrefix = [&](const std::string& p) {
            return arg.size() > p.size() - 1 && arg.compare(0, p.size(), p) == 0;
        };

        try {
            if (hasPrefix("--mode=")) {
                std::string v = arg.substr(7);
                if (v == "random") config.mode = InputMode::RANDOM;
                else if (v == "csv") config.mode = InputMode::CSV;
                else { std::cerr << "Unknown mode: " << v << "\n"; return false; }
            }
            else if (hasPrefix("--threshold=")) {
                config.threshold = std::stod(arg.substr(12));
            }
            else if (hasPrefix("--T_ns=")) {
                config.T_ns = std::stoull(arg.substr(7));
            }
            else if (hasPrefix("--columns=")) {
                config.columns = std::stoi(arg.substr(10));
            }
            else if (hasPrefix("--filter=")) {
                std::string v = arg.substr(9);
                if (v == "default") config.filter = FilterType::DEFAULT;
                else if (v == "file") config.filter = FilterType::FILE;
                else { std::cerr << "Unknown filter: " << v << "\n"; return false; }
            }
            else if (arg == "--stats") {
                config.stats = true;
            }
            else if (hasPrefix("--stats=")) {
                std::string v = arg.substr(8);
                std::transform(v.begin(), v.end(), v.begin(), ::tolower);
                config.stats = (v == "on" || v == "1" || v == "true");
            }
            else if (arg == "--quiet" || arg == "-q") {
                config.quiet = true;
            }
            else if (hasPrefix("--csv=")) {
                config.csvFile = arg.substr(6);
            }
            else if (hasPrefix("--filterfile=")) {
                config.filterFile = arg.substr(13);
            }
            else {
                std::cerr << "Unknown argument: " << arg << "\n";
                return false;
            }
        }
        catch (const std::exception& ex) {
            std::cerr << "Invalid value for argument '" << arg << "': " << ex.what() << "\n";
            return false;
        }
    }
    return true;
}

int main(int argc, char** argv) {
    // Parse CLI arguments
    Config config;
    if (!parseArgs(argc, argv, config)) {
        return 1;
    }

    // Interactive fallback for missing values
    if (config.threshold <= 0) {
        std::cout << "Enter threshold (TV): ";
        std::cin >> config.threshold;
    }
    if (config.T_ns < 500) {
        std::cout << "Enter process time T (ns, >=500): ";
        std::cin >> config.T_ns;
    }
    if (config.mode == InputMode::CSV && config.csvFile.empty()) {
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "Enter CSV file path (press Enter to use \"test.csv\"): ";
        std::string input;
        std::getline(std::cin, input);
        if (!input.empty()) config.csvFile = input;
        else config.csvFile = "test.csv";
    }
    if (config.mode == InputMode::CSV) {
        int probed = CsvStreamer::probeColumns(config.csvFile);
        if (probed <= 0) {
            std::cerr << "Failed to read CSV or zero columns detected. Exiting.\n";
            return 1;
        }
        config.columns = probed;
        if (!config.quiet) {
            std::cout << "Detected columns (m) = " << config.columns << "\n";
        }
    } else if (config.columns <= 0) {
        std::cout << "Enter columns (m): ";
        std::cin >> config.columns;
    }
    if (config.columns <= 0) {
        std::cerr << "Invalid columns (m). Exiting.\n";
        return 0;
    }

    // Create shared resources
    const size_t pairsCapacity = 128;
    ThreadSafeQueue<DataPair> queue(pairsCapacity);
    MetricsCollector* metrics = config.stats ? CreateFileMetricsCollector("pair_metrics.csv") : nullptr;

    // Build pipeline from config
    auto ctx = buildPipeline(config, &queue, metrics);

    if (!config.quiet) {
        std::cout << "Starting pipeline...\n";
    }
    ctx.pipeline.start();

    // Wait for completion
    if (config.mode == InputMode::CSV) {
        // Wait for generator to finish naturally
        if (ctx.generator) {
            while (ctx.generator->isRunning()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    } else {
        // Random mode: wait for user input
        if (std::cin.rdbuf()->in_avail() > 0 && std::cin.peek() == '\n') {
            std::cin.get();
        }
        std::string dummy;
        std::getline(std::cin, dummy);

        // Signal shutdown
        DataPair sentinel{};
        sentinel.seq = std::numeric_limits<uint64_t>::max();
        queue.push(sentinel);
    }

    if (!config.quiet) {
        std::cout << "Stopping pipeline...\n";
    }
    ctx.pipeline.stop();

    if (!config.quiet) {
        ctx.pipeline.printStats();
    }

    if (metrics) {
        delete metrics;
    }

    return 0;
}

