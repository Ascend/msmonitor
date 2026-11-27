// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#include "StepMetricReporter.h"

#include <glog/logging.h>

#include <algorithm>
#include <chrono>

namespace msmonitor::process
{
namespace
{
using Labels = std::map<std::string, std::string>;

Labels StepLabels(const std::string &job, const std::string &run, std::int64_t rank)
{
    Labels labels{{"run_id", run}, {"rank", std::to_string(rank)}};
    if (!job.empty())
    {
        labels.emplace("job_name", job);
    }
    return labels;
}

void AppendSamples(std::vector<MetricSample> &samples, const Labels &labels, std::int64_t timestampMs,
                   std::initializer_list<std::pair<const char *, double>> values)
{
    for (const auto &[name, value] : values)
    {
        samples.push_back({"msmonitor_" + std::string(name), labels, value, timestampMs});
    }
}
}  // namespace

StepMetricReporter::StepMetricReporter(std::string job, std::string run, std::string url, std::string token,
                                       std::string caFile)
    : job_(std::move(job)),
      run_(std::move(run)),
      transport_(std::move(url), std::move(token), std::move(caFile)),
      exporter_(
          {}, [this](const StepMetricData &step) { AppendStep(step); },
          [this](const StepSummary &summary) { AppendSummary(summary); })
{
}

void StepMetricReporter::ExportStep(const StepDataOutput &step)
{
    const auto exportStart = std::chrono::steady_clock::now();
    samples_.clear();
    // Keep every series aligned to the same increasing timestamp for this rank.
    // The original step bounds are also reported as sample values.
    sampleTimestampMs_ = std::max(sampleTimestampMs_ + 1, static_cast<std::int64_t>(step.boundary.endNs / 1000000));
    exporter_.ExportStep(step);
    const auto reportStart = std::chrono::steady_clock::now();
    const auto computeUs = std::chrono::duration_cast<std::chrono::microseconds>(reportStart - exportStart);
    try
    {
        transport_.Write(samples_);
    }
    catch (const RemoteWriteError &error)
    {
        const auto reportUs =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - reportStart);
        LOG(ERROR) << "Process step metrics dropped after Remote Write failure, rank: " << step.rank
                   << ", step: " << step.boundary.stepId << ", samples: " << samples_.size()
                   << ", remote_write_us: " << reportUs.count() << ", error: " << error.what();
        return;
    }
    const auto reportEnd = std::chrono::steady_clock::now();
    const auto reportUs = std::chrono::duration_cast<std::chrono::microseconds>(reportEnd - reportStart);
    const auto totalUs = std::chrono::duration_cast<std::chrono::microseconds>(reportEnd - exportStart);
    LOG(INFO) << "Process step metrics complete, rank: " << step.rank << ", step: " << step.boundary.stepId
              << ", samples: " << samples_.size() << ", metric_compute_us: " << computeUs.count()
              << ", remote_write_us: " << reportUs.count() << ", total_us: " << totalUs.count();
}

void StepMetricReporter::AppendStep(const StepMetricData &step)
{
    const auto &[id, start, end] = step.boundary;
    const auto seconds = (end - start) / MetricConstants::NsPerSecond;
    const auto labels = StepLabels(job_, run_, step.rank);
    AppendSamples(samples_, labels, sampleTimestampMs_,
                  {{"step_index", static_cast<double>(id)}, {"iteration_duration_seconds", seconds}, {"rank_info", 1}});
    for (const auto &[device, analysis] : step.analysis->devices)
    {
        auto scoped = labels;
        scoped["device"] = std::to_string(device);
        const auto coverage = [&](const char *scope, const Intervals &intervals)
        {
            scoped["scope"] = scope;
            AppendSamples(
                samples_, scoped, sampleTimestampMs_,
                {{"activity_coverage_ratio", static_cast<double>(IntervalLength(intervals)) / (end - start)}});
        };
        coverage("kernel_active", analysis.computing);
        coverage("communication", analysis.communication);
        coverage("overlap", analysis.overlap);
        coverage("uncovered_communication", analysis.uncoveredCommunication);
        coverage("observed_free", analysis.free);
    }
}

void StepMetricReporter::AppendSummary(const StepSummary &summary)
{
    const auto end = summary.endNs;
    const auto labels = StepLabels(job_, run_, summary.rank);
    AppendSamples(samples_, labels, sampleTimestampMs_,
                  {{"step_summary_valid", static_cast<double>(summary.valid)},
                   {"step_start_timestamp_seconds", summary.startNs / MetricConstants::NsPerSecond},
                   {"step_end_timestamp_seconds", end / MetricConstants::NsPerSecond}});
    for (const auto &[key, totals] : summary.ranges)
    {
        auto scoped = labels;
        scoped.insert({{"source", key.source}, {"domain", key.domain}, {"range_name", key.name}});
        AppendSamples(samples_, scoped, sampleTimestampMs_,
                      {{"range_time_seconds", totals.sum / totals.count},
                       {"range_time_sum_seconds", totals.sum},
                       {"range_calls", static_cast<double>(totals.count)}});
    }
    for (const auto &[key, stats] : summary.operators)
    {
        auto scoped = labels;
        scoped.insert({{"device", std::to_string(key.device)},
                       {"stream", std::to_string(key.stream)},
                       {"operator_scope", "stream"},
                       {"operator_kind", key.kind},
                       {"operator_type", key.name},
                       {"execution_type", key.type}});
        const auto &clusters = summary.kernels.at(key);
        AppendSamples(samples_, scoped, sampleTimestampMs_,
                      {{"operator_step_id", static_cast<double>(summary.stepId)},
                       {"operator_calls", static_cast<double>(stats.count)},
                       {"operator_duration_total_seconds", stats.total},
                       {"operator_duration_mean_seconds", stats.mean},
                       {"operator_duration_min_seconds", stats.minimum},
                       {"operator_duration_max_seconds", stats.maximum},
                       {"operator_duration_p50_seconds", stats.p50},
                       {"operator_duration_p99_seconds", stats.p99},
                       {"operator_cluster_count", static_cast<double>(clusters.size())}});
        for (std::size_t i = 0; i < clusters.size(); ++i)
        {
            const auto &cluster = clusters[i];
            scoped["cluster"] = std::to_string(i);
            AppendSamples(samples_, scoped, sampleTimestampMs_,
                          {{"operator_cluster_calls", static_cast<double>(cluster.count)},
                           {"operator_cluster_p50_seconds", cluster.p50},
                           {"operator_cluster_p99_seconds", cluster.p99},
                           {"operator_cluster_min_seconds", cluster.minimum},
                           {"operator_cluster_max_seconds", cluster.maximum}});
        }
    }
}
}  // namespace msmonitor::process
