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

    // Add blocks based on pipeline configuration
    for (const auto& blockName : config.pipelineBlocks) {
        if (blockName == "filter") {
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
        // Future blocks will go here:
        // else if (blockName == "transform") {
        //     auto transform = std::make_unique<TransformBlock>(...);
        //     ctx.pipeline.addBlock(std::move(transform));
        // }
        // else if (blockName == "aggregator") {
        //     auto aggregator = std::make_unique<AggregatorBlock>(...);
        //     ctx.pipeline.addBlock(std::move(aggregator));
        // }
        else {
            std::cerr << "[Pipeline] Warning: Unknown block type '" << blockName 
                      << "' - skipping\n";
        }
    }

    return ctx;
}