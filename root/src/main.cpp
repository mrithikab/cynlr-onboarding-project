#include <iostream>
#include "ThreadSafeQueue.h"
#include "DataGenerator.h"
#include "FilterBlock.h"

int main() {
    ThreadSafeQueue<DataPair> queue;

    DataGenerator gen(&queue);
    FilterBlock filter(0.3, &queue);

    gen.start();
    filter.start();

    std::cout << "Running... Press ENTER to stop.\n";
    std::cin.get();

    gen.stop();
    filter.stop();

    return 0;
}
