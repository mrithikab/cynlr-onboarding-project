#include <iostream>
#include "ThreadSafeQueue.h"
#include "DataGenerator.h"
#include "FilterBlock.h"
//not changed

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

    InputMode mode = (modeChoice == 0)
        ? InputMode::RANDOM
        : InputMode::CSV;

    std::string csvPath = "test.csv";
    if (mode == InputMode::CSV) {
        // consume leftover newline
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        std::cout << "Enter CSV file path (press Enter to use \"test.csv\"): ";
        std::string input;
        std::getline(std::cin, input);
        if (!input.empty()) csvPath = input;

        
        int probed = probeCSVColumns(csvPath);
        if (probed <= 0) {
            std::cerr << "Failed to read CSV or zero columns detected. Exiting.\n";
            return 1;
        }
        m = probed;
        std::cout << "Detected columns (m) = " << m << "\n";
    }
    else {
        std::cout << "Enter columns (m) ";
        std::cin >> m;
    }
    if (m <= 0) {
        std::cerr << "Invalid columns (m). Exiting.\n";
        return 1;
    }

    // Enforce steady-state memory bound: pairsCapacity = max(1, floor(m/4))
    size_t pairsCapacity = static_cast<size_t>(m) / 4;
    if (pairsCapacity < 1) pairsCapacity = 1;

    ThreadSafeQueue<DataPair> queue(pairsCapacity);

    DataGenerator gen(&queue, m, T, mode, csvPath);
    FilterBlock filter(m, TV, &queue);

    // Start consumer first and wait for readiness, then start producer
    filter.start();
    while (!filter.isReady()) std::this_thread::yield();
    gen.start();

    if (mode == InputMode::CSV) {
        // Wait for generator to finish reading the CSV (it will push the sentinel).
        while (gen.isRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Ensure generator thread joined
        gen.stop();

        // Give a moment for consumer to drain the queue (optional)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        // FilterBlock will exit after seeing sentinel pushed by the generator.
        filter.stop();
    }
    else {
        // Interactive: user stops manually
        std::cin.get();
        std::cin.get();

        gen.stop();
        // Push sentinel to wake and stop filter consumer(s)
        DataPair sentinel{};
        sentinel.seq = std::numeric_limits<uint64_t>::max();
        queue.push(sentinel);
        filter.stop();
    }
}

