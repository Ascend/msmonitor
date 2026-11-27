// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#include "StepMetricExporter.h"

#include <stdexcept>
#include <utility>

#include <glog/logging.h>

namespace msmonitor::process {
StepMetricExporter::StepMetricExporter(MetricOptions options, StepMetricSink stepSink, StepSummarySink summarySink)
    : options_(std::move(options)), stepSink_(std::move(stepSink)), summarySink_(std::move(summarySink)) {
    if (!options_.maxGroups || !options_.maxSamplesPerGroup || !options_.maxSamplesPerStep || !stepSink_ ||
        !summarySink_) {
        throw std::invalid_argument("invalid metric options or sinks");
    }
}

void StepMetricExporter::ExportStep(const StepDataOutput &step) {
    auto metrics = CollectStepMetrics(step);
    stepSink_(metrics);
    summarySink_(BuildStepSummary(std::move(metrics)));
}

StepMetricData StepMetricExporter::CollectStepMetrics(const StepDataOutput &step) const {
    StepMetricData result{step.rank, step.boundary, step.completion,
        step.analysis ? step.analysis : AnalyseStepIntervals(step), {}, {}, {}};
    for (const auto &event : step.ownedEvents) {
        const auto &data = event->data;
        const auto kind = data.value("kind", "");
        const auto duration = (event->endNs - event->startNs) / MetricConstants::NsPerSecond;
        if (kind == "Kernel" || kind == "Communication") {
            const auto type = data.value("type", kind);
            const auto originalName = data.value("name", "unknown");
            // Split only at an explicit token boundary: Add and AddN remain distinct.
            const auto name = kind == "Kernel" ? originalName.substr(0, originalName.find('_')) : originalName;
            if (kind == "Kernel") {
                auto &totals = result.kernelTypes[type];
                ++totals.count;
                totals.sum += duration;
            }
            const KernelKey key{
                data.at("deviceId").get<std::uint64_t>(), data.at("streamId").get<std::uint64_t>(), type, name, kind};
            result.kernels[key].push_back(duration);
        } else if (kind == "Marker" && event->endNs > event->startNs) {
            const auto name = data.value("name", "");
            // Step markers define ownership; user ranges are discovered without a name allowlist.
            if (name.rfind("step ", 0) != 0) {
                auto &totals =
                    result.ranges[{data.value("sourceKind", "unknown"), data.value("domain", "default"), name}];
                ++totals.count;
                totals.sum += duration;
            }
        }
    }
    return result;
}

StepSummary StepMetricExporter::BuildStepSummary(StepMetricData step) const {
    StepSummary summary;
    summary.rank = step.rank;
    summary.stepId = step.boundary.stepId;
    summary.startNs = step.boundary.startNs;
    summary.endNs = step.boundary.endNs;
    summary.complete = step.completion != StepCompletion::Stopped;
    summary.valid = summary.complete;
    summary.ranges = std::move(step.ranges);
    std::size_t sampleCount = 0;
    for (auto &[key, samples] : step.kernels) {
        if (summary.operators.size() >= options_.maxGroups || samples.size() > options_.maxSamplesPerGroup ||
            samples.size() > options_.maxSamplesPerStep - sampleCount) {
            LOG(WARNING) << "Process operator summary exceeds sample budget, rank: " << step.rank
                         << ", step: " << step.boundary.stepId << ", operator: " << key.name
                         << ", groups: " << step.kernels.size() << ", group samples: " << samples.size()
                         << ", preceding samples: " << sampleCount;
            summary.valid = false;
            break;
        }
        summary.operators.emplace(key, SummariseDurations(samples));
        summary.kernels.emplace(key, ClusterDurations(samples));
        sampleCount += samples.size();
        if (options_.includeOperatorSamples) {
            summary.operatorSamples.emplace(key, std::move(samples));
        }
    }
    // Never publish a truncated distribution as a valid step summary.
    if (!summary.valid) {
        summary.operators.clear();
        summary.kernels.clear();
        summary.operatorSamples.clear();
    }
    return summary;
}
} // namespace msmonitor::process
