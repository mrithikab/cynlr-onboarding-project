#include <iostream>
#include "ThreadSafeQueue.h"
#include "DataGenerator.h"
#include "FilterBlock.h"
#include "stream/CsvStreamer.h"
#include "metrics/Collectors.h"
#include "profiler/ModuleProfiler.h"
#include <direct.h>
#include <limits.h>

int main() {
    int m;
    double TV;
    uint64_t T;
    int modeChoice;

    std::cout << "Enter threshold (TV): ";
    std::cin >> TV;

    std::cout << "Enter process time T (ns, >=500): ";
    std::cin >> T;

    std::cout << "Select mode (0 = Random, 1 = CSV): ";
    std::cin >> modeChoice;

    InputMode mode = (modeChoice == 0) ? InputMode::RANDOM : InputMode::CSV;

    std::string csvPath = "test.csv";
    if (mode == InputMode::CSV) {
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "Enter CSV file path (press Enter to use \"test.csv\"): ";
        std::string input;
        std::getline(std::cin, input);
        if (!input.empty()) csvPath = input;

        int probed = CsvStreamer::probeColumns(csvPath);
        if (probed <= 0) {
            std::cerr << "Failed to read CSV or zero columns detected. Exiting.\n";
            return 1;
        }
        m = probed;
        std::cout << "Detected columns (m) = " << m << "\n";
    } else {
        std::cout << "Enter columns (m) ";
        std::cin >> m;
    }
    if (m <= 0) {
        std::cerr << "Invalid columns (m). Exiting.\n";
        return 0;
    }

    const size_t pairsCapacity = 1024;
    ThreadSafeQueue<DataPair> queue(pairsCapacity);

    MetricsCollector* metrics = nullptr;
    std::cout << "Enable file metrics (writes pair_metrics.csv)? (y/N): ";
    char metricsChoice = 'n';
    std::cin >> metricsChoice;
    if (metricsChoice == 'y' || metricsChoice == 'Y') {
        metrics = CreateFileMetricsCollector("pair_metrics.csv");
    }

    DataGenerator gen(&queue, m, T, mode, csvPath);
    FilterBlock filter(m, TV, &queue, metrics);
    std::cout << "press Enter to end \n";
    filter.start();
    while (!filter.isReady()) std::this_thread::yield();
    gen.start();

    if (mode == InputMode::CSV) {
        while (gen.isRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        gen.stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        filter.stop();

        if (metrics) {
            delete metrics;
            metrics = nullptr;
        }
    } else {
        std::cin.get();
        std::cin.get();

        gen.stop();
        DataPair sentinel{};
        sentinel.seq = std::numeric_limits<uint64_t>::max();
        queue.push(sentinel);
        filter.stop();

        if (metrics) {
            delete metrics;
            metrics = nullptr;
        }
    }

    char cwdBuf[_MAX_PATH] = {0};
    if (_getcwd(cwdBuf, _MAX_PATH) == nullptr) {
        ModuleProfiler::Instance().recordCounter("startup_probe", 1);
        ModuleProfiler::Instance().flushToFile("module_profile.csv");
        std::cout << "ModuleProfiler flushed to: module_profile.csv (working directory unknown)\n";
    } else {
        std::string outPath = std::string(cwdBuf) + "\\module_profile.csv";
        ModuleProfiler::Instance().recordCounter("startup_probe", 1);
        ModuleProfiler::Instance().flushToFile(outPath);
        std::cout << "ModuleProfiler flushed to: " << outPath << "\n";
    }

    if (metrics) {
        metrics->flush();
        delete metrics;
        metrics = nullptr;
    }

    return 0;
}

