#include "profiler/ModuleProfiler.h"

// No-op stub implementation for unit tests.
// Add this file to your TestDataGenerator project so tests don't need the real profiler.

ModuleProfiler& ModuleProfiler::Instance() noexcept {
    static ModuleProfiler inst;
    return inst;
}

ModuleProfiler::ModuleProfiler() {}
ModuleProfiler::~ModuleProfiler() {}

void ModuleProfiler::recordSample(const std::string&, uint64_t) {}
void ModuleProfiler::recordCounter(const std::string&, uint64_t) {}
void ModuleProfiler::flushToFile(const std::string&) {}

ModuleProfiler::ScopedTimer::ScopedTimer(const std::string&) {}
ModuleProfiler::ScopedTimer::~ScopedTimer() {}