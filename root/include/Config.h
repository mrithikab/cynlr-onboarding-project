#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "DataGenerator.h" // for InputMode

// Configuration enums
enum class FilterType {
    DEFAULT,
    FILE
};

// Main configuration structure
struct Config {
    // Data source configuration
    InputMode mode = InputMode::RANDOM;
    std::string csvFile = "test.csv";
    int columns = 1024;
    uint64_t T_ns = 1000;

    // Filter configuration
    double threshold = 400.0;
    FilterType filter = FilterType::DEFAULT;
    std::string filterFile = "";

    // Pipeline configuration
    std::vector<std::string> pipelineBlocks = {"filter"};  // Default pipeline

    // Metrics and profiling
    bool stats = false;
    
    // Output control
    bool quiet = false;  // Suppress all non-error output
};