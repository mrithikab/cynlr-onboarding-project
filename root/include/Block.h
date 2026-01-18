#pragma once
#include <string>

// Forward declare to avoid circular includes
struct DataPair;

// Thin abstract interface for all pipeline blocks.
class Block {
public:
    virtual ~Block() = default;

    // Lifecycle
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isReady() const = 0;

    // Observability
    virtual void printStats() const = 0;
    virtual std::string name() const = 0;

    // Output interface (default = no-op for blocks with no downstream output)
    virtual void emit(const DataPair& pair) {
        // Default: do nothing (inherited by blocks like FilterBlock that don't emit)
        (void)pair; // suppress unused parameter warning
    }
};