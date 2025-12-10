#include "DataGenerator.h"
#include <random>
#include <chrono>
#include <thread>

DataGenerator::DataGenerator(ThreadSafeQueue<DataPair>* q)
    : queue(q), running(false) {
}

void DataGenerator::start() {
    running = true;
    worker = std::thread(&DataGenerator::run, this);
}

void DataGenerator::stop() {
    running = false;
    if (worker.joinable()) worker.join();
}

void DataGenerator::run() {
    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    while (running) {
        DataPair pair{ dist(rng), dist(rng) };
        queue->push(pair);
       std::this_thread::sleep_for(std::chrono::microseconds(100)); //to throttle producer 
        
    }
}
