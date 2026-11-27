// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#pragma once

#include <limits>
#include <map>
#include <tuple>

#include "StepAnalysis.h"

namespace msmonitor::process
{
struct MetricConstants final
{
    static constexpr double NsPerSecond{1e9};
    static constexpr std::size_t KdeGrid{512};
    static constexpr std::size_t MinKdeSamples{12};
    static constexpr std::size_t MinClusterSamples{5};
    static constexpr std::size_t MaxGroups{256};
    static constexpr std::size_t MaxSamplesPerGroup{65536};
    static constexpr std::size_t MaxSamplesPerStep{262144};
    static constexpr double MinSigma{1e-6};
    static constexpr double ValleyProminence{0.02};
    static constexpr double ValleySpacing{0.5};
    static constexpr std::uint64_t AllDevices{std::numeric_limits<std::uint64_t>::max()};
};

struct MetricOptions
{
    std::size_t maxGroups{MetricConstants::MaxGroups};
    std::size_t maxSamplesPerGroup{MetricConstants::MaxSamplesPerGroup};
    std::size_t maxSamplesPerStep{MetricConstants::MaxSamplesPerStep};
    bool includeOperatorSamples{false};
};

struct RangeKey
{
    std::string source;
    std::string domain;
    std::string name;
    bool operator<(const RangeKey &other) const
    {
        return std::tie(source, domain, name) < std::tie(other.source, other.domain, other.name);
    }
};

struct DurationTotals
{
    std::uint64_t count{0};
    double sum{0};
};

struct KernelKey
{
    std::uint64_t device;
    std::uint64_t stream;
    std::string type;
    std::string name;
    std::string kind{"Kernel"};

    bool operator<(const KernelKey &other) const
    {
        return std::tie(device, stream, type, name, kind) <
               std::tie(other.device, other.stream, other.type, other.name, other.kind);
    }
};

struct OperatorStatistics
{
    std::uint64_t count;
    double total;
    double mean;
    double minimum;
    double maximum;
    double p50;
    double p99;
};

struct StepMetricData
{
    std::int64_t rank;
    StepBoundary boundary;
    StepCompletion completion;
    std::shared_ptr<const StepAnalysis> analysis;
    std::map<RangeKey, DurationTotals> ranges;
    std::map<std::string, DurationTotals> kernelTypes;
    std::map<KernelKey, std::vector<double>> kernels;
};

struct KernelCluster
{
    std::uint64_t count;
    double p50;
    double p99;
    double minimum;
    double maximum;
};

struct StepSummary
{
    std::int64_t rank{-1};
    std::uint64_t stepId{0};
    std::uint64_t startNs{0};
    std::uint64_t endNs{0};
    bool complete{false};
    bool valid{true};
    std::map<RangeKey, DurationTotals> ranges;
    std::map<KernelKey, std::vector<KernelCluster>> kernels;
    std::map<KernelKey, OperatorStatistics> operators;
    // Optional handoff to the offline cross-rank aggregator; not retained in history.
    std::map<KernelKey, std::vector<double>> operatorSamples;
};

using StepMetricSink = std::function<void(const StepMetricData &)>;
using StepSummarySink = std::function<void(StepSummary)>;

double Quantile(std::vector<double> values, double probability);
std::vector<KernelCluster> ClusterDurations(const std::vector<double> &durations);
OperatorStatistics SummariseDurations(std::vector<double> &samples);
}  // namespace msmonitor::process
