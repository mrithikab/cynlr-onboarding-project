#include <iostream>
#include <vector>
#include <cmath>
#include <numeric>
#include <string>
#include "FilterBlock.h"  

// Pure FIR function for unit testing

static double applyFIR(const std::vector<double>& window, const std::vector<double>& kernel) {
    double sum = 0;
    
    // kernel[0] multiplies with window[0] (oldest)
    for (size_t i = 0; i < kernel.size(); ++i)
        sum += window[i] * kernel[i];
    return sum;
}

static void fail(const std::string &msg) {
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}
static void pass(const std::string &msg) {
    std::cout << "PASS: " << msg << "\n";
}

void testFIRSumOfOnes() {
    std::vector<double> kernel(9, 1.0);
    std::vector<double> input = {1,2,3,4,5,6,7,8,9,10};
    std::vector<double> expected;
    for (size_t i = 8; i < input.size(); ++i) {
        std::vector<double> window(input.begin() + i - 8, input.begin() + i + 1);
        expected.push_back(applyFIR(window, kernel));
    }
    std::vector<double> actual;
    std::vector<double> window;
    for (double v : input) {
        window.push_back(v);
        if (window.size() > 9) window.erase(window.begin());
        if (window.size() == 9)
            actual.push_back(applyFIR(window, kernel));
    }
    if (actual.size() != expected.size()) fail("Output count mismatch");
    for (size_t i = 0; i < actual.size(); ++i) {
        if (std::abs(actual[i] - expected[i]) > 1e-9)
            fail("Mismatch at index " + std::to_string(i));
    }
    pass("FIR sum-of-ones kernel correct");
}

void testFIRImpulse() {
    std::vector<double> kernel = {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9};
    size_t K = kernel.size();

    // Impulse at position K-1 (index 8)
    std::vector<double> input(2 * K - 1, 0.0);
    input[K - 1] = 1.0;

    // For non-causal filter with kernel[0] on oldest:
    // Expected output is kernel in REVERSE order
    std::vector<double> expected = {0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.2, 0.1};
    
    std::vector<double> actual;
    std::vector<double> window;
    for (double v : input) {
        window.push_back(v);
        if (window.size() > K) window.erase(window.begin());
        if (window.size() == K)
            actual.push_back(applyFIR(window, kernel));
    }
    if (actual.size() != expected.size()) fail("Impulse output count mismatch");
    for (size_t i = 0; i < actual.size(); ++i) {
        if (std::abs(actual[i] - expected[i]) > 1e-9)
            fail("Impulse mismatch at index " + std::to_string(i));
    }
    pass("FIR impulse response correct");
}

void testFIRConstant() {
    std::vector<double> kernel = {0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5};
    std::vector<double> input(20, 5.0);
    double expected_val = 5.0 * std::accumulate(kernel.begin(), kernel.end(), 0.0);
    std::vector<double> actual;
    std::vector<double> window;
    for (double v : input) {
        window.push_back(v);
        if (window.size() > 9) window.erase(window.begin());
        if (window.size() == 9)
            actual.push_back(applyFIR(window, kernel));
    }
    for (size_t i = 0; i < actual.size(); ++i) {
        if (std::abs(actual[i] - expected_val) > 1e-9)
            fail("Constant mismatch at index " + std::to_string(i));
    }
    pass("FIR constant signal correct");
}

void testFIRPairBoundary() {
    std::vector<double> kernel(9, 1.0);
    std::vector<double> input;
    for (int i = 0; i < 20; ++i) input.push_back((i % 2) + 1);
    std::vector<double> actual;
    std::vector<double> window;
    for (double v : input) {
        window.push_back(v);
        if (window.size() > 9) window.erase(window.begin());
        if (window.size() == 9)
            actual.push_back(applyFIR(window, kernel));
    }
    for (size_t i = 0; i < actual.size(); ++i) {
        double expected = 0;
        for (int k = 0; k < 9; ++k) expected += input[i + k];
        if (std::abs(actual[i] - expected) > 1e-9)
            fail("Pair-boundary mismatch at index " + std::to_string(i));
    }
    pass("FIR pair-boundary correctness");
}

// ============================================================
// Tests using actual FilterBlock
// ============================================================

void testFilterBlockAgainstReference() {
    FilterBlock fb(12, 500.0, nullptr, nullptr, false, "");
    
    std::vector<double> input = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0};
    std::vector<double> kernel(fb.fir_kernel, fb.fir_kernel + 9);
    
    std::vector<double> expected;
    std::vector<double> window;
    for (double v : input) {
        window.push_back(v);
        if (window.size() > 9) window.erase(window.begin());
        if (window.size() == 9) {
            expected.push_back(applyFIR(window, kernel));
        }
    }
    
    std::vector<double> actual;
    for (size_t i = 8; i < input.size(); ++i) {
        std::vector<double> samples(input.begin(), input.begin() + i + 1);
        double result = fb.testApplyFIR(samples);
        actual.push_back(result);
    }
    
    if (actual.size() != expected.size()) {
        fail("FilterBlock: output count mismatch");
    }
    
    for (size_t i = 0; i < actual.size(); ++i) {
        if (std::abs(actual[i] - expected[i]) > 1e-6) {
            std::cerr << "Index " << i << ": expected=" << expected[i] 
                      << " actual=" << actual[i] << "\n";
            fail("FilterBlock: output mismatch at index " + std::to_string(i));
        }
    }
    
    pass("FilterBlock matches reference implementation");
}

void testFilterBlockWithDefaultKernel() {
    FilterBlock fb(12, 500.0, nullptr, nullptr, false, "");
    
    std::vector<double> input = {100.0, 100.0, 100.0, 100.0, 100.0, 100.0, 100.0, 100.0, 100.0};
    double result = fb.testApplyFIR(input);
    
    double expected = 0.0;
    for (int i = 0; i < 9; ++i) {
        expected += fb.fir_kernel[i] * 100.0;
    }
    
    if (std::abs(result - expected) > 1e-6) {
        std::cerr << "Expected: " << expected << ", Got: " << result << "\n";
        fail("FilterBlock default kernel calculation error");
    }
    
    pass("FilterBlock default kernel calculation");
}

void testFilterBlockCircularBuffer() {
    FilterBlock fb(12, 500.0, nullptr, nullptr, false, "");
    
    std::vector<double> samples = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    double result = fb.testApplyFIR(samples);
    
    std::vector<double> last9(samples.end() - 9, samples.end());
    std::vector<double> kernel(fb.fir_kernel, fb.fir_kernel + 9);
    double expected = applyFIR(last9, kernel);
    
    if (std::abs(result - expected) > 1e-6) {
        std::cerr << "Expected: " << expected << ", Got: " << result << "\n";
        fail("FilterBlock circular buffer wrapping error");
    }
    
    pass("FilterBlock circular buffer wrapping");
}

void testNonCausalAlignment() {
    std::cout << "\n=== Testing Non-Causal Filter Alignment ===\n";
    FilterBlock fb(12, 500.0, nullptr, nullptr, false, "");
    
    // Impulse at position 4 (center)
    std::vector<double> impulse = {0, 0, 0, 0, 1, 0, 0, 0, 0};
    double result = fb.testApplyFIR(impulse);
    
    double expected = fb.fir_kernel[4];
    
    std::cout << "Impulse test: kernel[4] = " << fb.fir_kernel[4] << "\n";
    std::cout << "Result: " << result << "\n";
    
    if (std::abs(result - expected) > 1e-6) {
        std::cerr << "Expected kernel[4]=" << expected << ", Got: " << result << "\n";
        fail("Non-causal alignment incorrect");
    }
    
    pass("Non-causal filter: kernel[4] aligns with center");
}

int main() {
    std::cout << "Running FilterBlock FIR calculation unit tests...\n\n";
    
    std::cout << "Testing reference FIR implementation...\n";
    testFIRSumOfOnes();
    testFIRImpulse();
    testFIRConstant();
    testFIRPairBoundary();
    
    std::cout << "\nTesting actual FilterBlock implementation...\n";
    testFilterBlockAgainstReference();
    testFilterBlockWithDefaultKernel();
    testFilterBlockCircularBuffer();
    testNonCausalAlignment();
    
    std::cout << "\nAll FilterBlock FIR calculation tests passed.\n";
    return 0;
}
