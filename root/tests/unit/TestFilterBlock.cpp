#include <iostream>
#include <fstream>
#include <string>
#include "FilterBlock.h"

static void fail(const std::string &msg) {
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}
static void pass(const std::string &msg) {
    std::cout << "PASS: " << msg << "\n";
}

void testFilterBlockKernelFile() {
    FilterBlock fb(4, 1.0, nullptr);
    // Malformed: non-numeric
    {
        const std::string path = "test_kernel_non_numeric.txt";
        std::ofstream f(path);
        f << "0.1 0.2 abc 0.4 0.5 0.6 0.7 0.8 0.9";
        if (fb.loadKernelFromFile(path)) fail("FilterBlock accepted non-numeric kernel value");
    }
    // Malformed: NaN/Inf
    {
        const std::string path = "test_kernel_nan.txt";
        std::ofstream f(path);
        f << "0.1 0.2 nan 0.4 0.5 0.6 0.7 0.8 0.9";
        if (fb.loadKernelFromFile(path)) fail("FilterBlock accepted NaN kernel value");
    }
    // Malformed: too few values
    {
        const std::string path = "test_kernel_few.txt";
        std::ofstream f(path);
        f << "0.1 0.2 0.3";
        if (fb.loadKernelFromFile(path)) fail("FilterBlock accepted too few kernel values");
    }
    // Malformed: too many values
    {
        const std::string path = "test_kernel_many.txt";
        std::ofstream f(path);
        f << "0.1 0.2 0.3 0.4 0.5 0.6 0.7 0.8 0.9 1.0";
        if (fb.loadKernelFromFile(path)) fail("FilterBlock accepted too many kernel values");
    }
    // Malformed: file not found
    {
        if (fb.loadKernelFromFile("nonexistent_kernel.txt")) fail("FilterBlock accepted nonexistent kernel file");
    }
    pass("FilterBlock kernel file error handling");
}

int main() {
    std::cout << "\nRunning FilterBlock kernel file unit tests...\n";
    testFilterBlockKernelFile();
    std::cout << "All FilterBlock kernel file tests passed.\n";
    return 0;
}
