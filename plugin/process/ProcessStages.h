// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#pragma once

#include "ProcessTypes.h"

namespace msmonitor::process
{

// Receives raw JSONL in read order and emits completed steps through the callback.
class StepAnalyser
{
   public:
    virtual ~StepAnalyser() = default;
    virtual std::int64_t Rank() const = 0;
    // The line reference is valid only during this call.
    virtual void ProcessLine(const JsonlLine &line) = 0;
    virtual void EndBatch(const ReadBatch &batch, const EmitStepData &emit) = 0;
    virtual void Finish(bool inputComplete, const EmitStepData &emit) = 0;
};

// Use a distinct exporter instance for each rank-local Processor.
// Metric-worker-only export: calculate metrics from shared intervals, build and report step summaries.
class MetricExporter
{
   public:
    virtual ~MetricExporter() = default;
    virtual void ExportStep(const StepDataOutput &step) = 0;
};

// Use a distinct exporter instance for each rank-local Processor.
// Called by the export worker only. Implementations select Chrome Trace or MindStudio DB.
class TraceExporter
{
   public:
    virtual ~TraceExporter() = default;

    virtual void WriteStep(StepDataPtr step) = 0;
    virtual void Flush() {}
};
}  // namespace msmonitor::process
