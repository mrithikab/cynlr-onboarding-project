#include "FilterBlock.h"
#include <iostream>

static constexpr double KERNEL[9] = {
    0.00025177,
    0.008666992,
    0.078025818,
    0.24130249,
    0.343757629,
    0.24130249,
    0.078025818,
    0.008666992,
    0.000125885
};

FilterBlock::FilterBlock(double TV, ThreadSafeQueue<DataPair>* q)
    : threshold(TV), queue(q), running(false)
{
    window.resize(9, 0.0);
}

void FilterBlock::start() {
    running = true;
    worker = std::thread(&FilterBlock::run, this);
}

void FilterBlock::stop() {
    running = false;
    if (worker.joinable()) worker.join();
}

double FilterBlock::applyFilter(double x) {
    for (int i = 0; i < 8; i++)
        window[i] = window[i + 1];
    window[8] = x;

    double sum = 0.0;
    for (int i = 0; i < 9; i++)
        sum += window[i] * KERNEL[i];

    return sum;
}

void FilterBlock::run() {
    while (running) {
        DataPair p = queue->pop();

        double f1 = applyFilter(p.a);
        double f2 = applyFilter(p.b);

        int out1 = (f1 >= threshold ? 1 : 0);
        int out2 = (f2 >= threshold ? 1 : 0);

        std::cout << "Input A=" << p.a
            << " Filtered=" << f1
            << " Out=" << out1 << "\n";

        std::cout << "Input B=" << p.b
            << " Filtered=" << f2
            << " Out=" << out2 << "\n";
    }
}
