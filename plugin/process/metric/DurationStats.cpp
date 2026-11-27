// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#include "MetricTypes.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace msmonitor::process {
namespace {
// Linear interpolation over an already sorted range; callers own sorting and validation.
double SortedQuantile(std::vector<double>::const_iterator begin, std::size_t size, double probability) {
    const auto position = probability * (size - 1);
    const auto lower = static_cast<std::size_t>(position);
    return begin[lower] + (begin[std::min(lower + 1, size - 1)] - begin[lower]) * (position - lower);
}

std::vector<double> CalculateDensity(const std::vector<double> &logs, double bandwidth) {
    // Nanosecond durations repeat frequently. Weighted identical samples preserve the KDE
    // while evaluating one Gaussian per distinct duration instead of per call.
    std::vector<std::pair<double, std::size_t>> frequencies;
    for (const auto value : logs) {
        if (frequencies.empty() || frequencies.back().first != value) {
            frequencies.emplace_back(value, 1);
        } else {
            ++frequencies.back().second;
        }
    }
    std::vector<double> density(MetricConstants::KdeGrid, 0);
    const auto spacing = (logs.back() - logs.front()) / (density.size() - 1);
    for (std::size_t i = 0; i < density.size(); ++i) {
        const auto x = logs.front() + i * spacing;
        for (const auto &[value, count] : frequencies) {
            const auto z = (x - value) / bandwidth;
            density[i] += count * std::exp(-0.5 * z * z);
        }
    }
    return density;
}

std::vector<std::size_t> FindClusterCuts(const std::vector<double> &logs, double bandwidth) {
    std::vector<std::size_t> cuts{0};
    if (logs.size() < MetricConstants::MinKdeSamples || bandwidth <= MetricConstants::MinSigma) {
        return cuts;
    }
    const auto density = CalculateDensity(logs, bandwidth);
    auto leftPeaks = density;
    auto rightPeaks = density;
    std::partial_sum(density.begin(), density.end(), leftPeaks.begin(),
        [](double left, double right) { return std::max(left, right); });
    std::partial_sum(density.rbegin(), density.rend(), rightPeaks.rbegin(),
        [](double left, double right) { return std::max(left, right); });
    const auto spacing = (logs.back() - logs.front()) / (density.size() - 1);
    auto previous = logs.front() - bandwidth;
    for (std::size_t i = 1; i + 1 < density.size(); ++i) {
        if (!(density[i] < density[i - 1] && density[i] <= density[i + 1])) {
            continue;
        }
        const auto x = logs.front() + i * spacing;
        const auto index = static_cast<std::size_t>(std::upper_bound(logs.begin(), logs.end(), x) - logs.begin());
        if (std::min(leftPeaks[i - 1], rightPeaks[i + 1]) - density[i] <
                MetricConstants::ValleyProminence * leftPeaks.back() ||
            x - previous < MetricConstants::ValleySpacing * bandwidth ||
            index - cuts.back() < MetricConstants::MinClusterSamples ||
            logs.size() - index < MetricConstants::MinClusterSamples) {
            continue;
        }
        cuts.push_back(index);
        previous = x;
    }
    return cuts;
}
} // namespace

OperatorStatistics SummariseDurations(std::vector<double> &samples) {
    if (samples.empty()) {
        throw std::invalid_argument("empty operator duration samples");
    }
    if (!std::is_sorted(samples.begin(), samples.end())) {
        std::sort(samples.begin(), samples.end());
    }
    const auto total = std::accumulate(samples.begin(), samples.end(), 0.0);
    return {samples.size(), total, total / samples.size(), samples.front(), samples.back(),
        SortedQuantile(samples.begin(), samples.size(), 0.5), SortedQuantile(samples.begin(), samples.size(), 0.99)};
}

double Quantile(std::vector<double> values, double probability) {
    if (values.empty() || !std::isfinite(probability) || probability < 0 || probability > 1) {
        throw std::invalid_argument("invalid quantile input");
    }
    std::sort(values.begin(), values.end());
    return SortedQuantile(values.begin(), values.size(), probability);
}

std::vector<KernelCluster> ClusterDurations(const std::vector<double> &durations) {
    std::vector<double> sorted;
    sorted.reserve(durations.size());
    for (const auto value : durations) {
        if (value > 0 && std::isfinite(value)) {
            sorted.push_back(value);
        }
    }
    if (sorted.empty()) {
        return {};
    }
    if (!std::is_sorted(sorted.begin(), sorted.end())) {
        std::sort(sorted.begin(), sorted.end());
    }
    std::vector<double> logs;
    logs.reserve(sorted.size());
    for (const auto value : sorted) {
        logs.push_back(std::log(value));
    }
    const auto mean = std::accumulate(logs.begin(), logs.end(), 0.0) / logs.size();
    double variance = 0;
    for (const auto value : logs) {
        variance += (value - mean) * (value - mean);
    }
    const auto bandwidth = 1.06 * std::sqrt(variance / logs.size()) * std::pow(logs.size(), -0.2);
    auto cuts = FindClusterCuts(logs, bandwidth);
    cuts.push_back(sorted.size());
    std::vector<KernelCluster> result;
    for (std::size_t i = 1; i < cuts.size(); ++i) {
        const auto begin = sorted.begin() + cuts[i - 1];
        const auto size = cuts[i] - cuts[i - 1];
        result.push_back(
            {size, SortedQuantile(begin, size, 0.5), SortedQuantile(begin, size, 0.99), *begin, sorted[cuts[i] - 1]});
    }
    return result;
}
} // namespace msmonitor::process
