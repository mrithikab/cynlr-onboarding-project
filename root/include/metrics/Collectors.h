
#pragma once
#include <string>
#include "metrics/MetricsCollector.h"

// Factory helpers implemented in src/metrics/*.cpp
// CreateFileMetricsCollector returns a heap-allocated MetricsCollector* (caller owns and must delete).
MetricsCollector * CreateFileMetricsCollector(const std::string & path);

// CreateNoopMetricsCollector returns a heap-allocated no-op MetricsCollector* (caller owns and must delete).
MetricsCollector* CreateNoopMetricsCollector();