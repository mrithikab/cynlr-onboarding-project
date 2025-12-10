#pragma once
#include <atomic>
#include <thread>
#include "ThreadSafeQueue.h"

struct DataPair { double a, b; };

class DataGenerator {
public:
    DataGenerator(ThreadSafeQueue<DataPair>* q);
    void start();
    void stop();

private:
    void run();

    std::thread worker;
    std::atomic<bool> running;
    ThreadSafeQueue<DataPair>* queue;
};
