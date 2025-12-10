#pragma once
#include <thread>
#include <atomic>
#include <vector>
#include "DataGenerator.h"
#include "ThreadSafeQueue.h"

class FilterBlock {
public:
    FilterBlock(double TV, ThreadSafeQueue<DataPair>* q);
    void start();
    void stop();

private:
    void run();
    double applyFilter(double x);

    std::thread worker;
    std::atomic<bool> running;
    ThreadSafeQueue<DataPair>* queue;
    std::vector<double> window;
    double threshold;
};
