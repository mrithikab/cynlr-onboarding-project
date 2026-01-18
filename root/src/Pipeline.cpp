#include "Pipeline.h"
#include "DataGenerator.h"
#include "FilterBlock.h"
#include <iostream>

PipelineContext buildPipeline(const Config& config,
                              ThreadSafeQueue<DataPair>* queue,
                              MetricsCollector* metrics)
{
    PipelineContext ctx;
    ctx.generator = nullptr;

    // Always add DataGenerator (source block)
    auto gen = std::make_unique<DataGenerator>(
        queue,
        config.columns,
        config.T_ns,
        config.mode,
        config.csvFile
    );
    ctx.generator = gen.get(); // store pointer before moving ownership
    ctx.pipeline.addBlock(std::move(gen));

    // Conditionally add FilterBlock
    if (config.enableFilter) {
        bool useFileKernel = (config.filter == FilterType::FILE);
        auto filter = std::make_unique<FilterBlock>(
            config.columns,
            config.threshold,
            queue,
            metrics,
            useFileKernel,
            config.filterFile
        );
        ctx.pipeline.addBlock(std::move(filter));
    }

    // Future: Add more blocks here based on config flags
    // if (config.enableTransform) { ctx.pipeline.addBlock(...); }
    // if (config.enableAggregator) { ctx.pipeline.addBlock(...); }

    return ctx;
}