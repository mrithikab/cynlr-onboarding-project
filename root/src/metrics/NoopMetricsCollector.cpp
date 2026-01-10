#include "metrics/MetricsCollector.h"

class NoopMetricsCollector : public MetricsCollector {
public:
    void recordPair(uint64_t, uint64_t, bool, uint64_t, uint64_t, uint64_t, uint64_t,
                    uint64_t, uint64_t, uint64_t, uint64_t) override {}
    void flush() override {}
};

MetricsCollector* CreateNoopMetricsCollector() {
    return new NoopMetricsCollector();
}