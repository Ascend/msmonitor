// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#pragma once

#include "MetricTypes.h"
#include "ProcessStages.h"

namespace msmonitor::process {
// One instance per rank, used exclusively by the existing metric worker.
class StepMetricExporter final : public MetricExporter {
  public:
    StepMetricExporter(MetricOptions options, StepMetricSink stepSink, StepSummarySink summarySink);
    void ExportStep(const StepDataOutput &step) override;

  private:
    StepMetricData CollectStepMetrics(const StepDataOutput &step) const;
    StepSummary BuildStepSummary(StepMetricData step) const;

    MetricOptions options_;
    StepMetricSink stepSink_;
    StepSummarySink summarySink_;
};
} // namespace msmonitor::process
