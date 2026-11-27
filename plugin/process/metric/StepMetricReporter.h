// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#pragma once

#include "PrometheusRemoteWrite.h"
#include "StepMetricExporter.h"

namespace msmonitor::process {
class StepMetricReporter final : public MetricExporter {
  public:
    StepMetricReporter(std::string job, std::string run, std::string url, std::string token, std::string caFile);
    void ExportStep(const StepDataOutput &step) override;

  private:
    void AppendStep(const StepMetricData &step);
    void AppendSummary(const StepSummary &summary);

    std::string job_;
    std::string run_;
    PrometheusRemoteWrite transport_;
    std::vector<MetricSample> samples_;
    StepMetricExporter exporter_;
};
} // namespace msmonitor::process
