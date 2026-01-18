#pragma once
#include "Block.h"
#include "Config.h"
#include "ThreadSafeQueue.h"
#include "DataGenerator.h"
#include "metrics/MetricsCollector.h"
#include <vector>
#include <memory>
#include <iostream>

// Pipeline manager: owns and orchestrates blocks
class Pipeline {
public:
    // Add a block (transfers ownership)
    void addBlock(std::unique_ptr<Block> block) {
        blocks_.push_back(std::move(block));
    }

    // Start all blocks in order
    void start() {
        for (auto& b : blocks_) {
            std::cout << "[Pipeline] Starting " << b->name() << "\n";
            b->start();
        }
    }

    // Stop all blocks in reverse order
    void stop() {
        for (auto it = blocks_.rbegin(); it != blocks_.rend(); ++it) {
            std::cout << "[Pipeline] Stopping " << (*it)->name() << "\n";
            (*it)->stop();
        }
    }

    // Print statistics for all blocks
    void printStats() const {
        std::cout << "\n=== Pipeline Statistics ===\n";
        for (const auto& b : blocks_) {
            std::cout << "\n[" << b->name() << "]\n";
            b->printStats();
        }
        std::cout << "===========================\n";
    }

private:
    std::vector<std::unique_ptr<Block>> blocks_; // owns blocks
};

// Context: pipeline + references to specific blocks for control flow
struct PipelineContext {
    Pipeline pipeline;
    DataGenerator* generator = nullptr; // <- add = nullptr
};

// Factory function: build pipeline from config
PipelineContext buildPipeline(const Config& config,
                              ThreadSafeQueue<DataPair>* queue,
                              MetricsCollector* metrics);