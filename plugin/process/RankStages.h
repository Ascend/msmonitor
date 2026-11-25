// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#pragma once

#include <cstdint>
#include <filesystem>

#include "ProcessStages.h"

// ==================== 临时空实现，待删除 ====================
// 仅用于让 Processor 的 stage 装配点可编译，不产生任何输出。
// 数据处理组件的正式实现（RankAnalyser / RankMetricExporter / RankTraceExporter）
// 合入后删除本文件，ProcessRunner 改为包含对应的实现头文件。
// ==========================================================

namespace msmonitor::process
{

// 占位实现：不解析任何行，也不产出 step。
class RankAnalyser final : public StepAnalyser
{
   public:
    explicit RankAnalyser(std::int64_t rank) : rank_(rank) {}

    std::int64_t Rank() const override { return rank_; }

    void ProcessLine(const JsonlLine &) override {}

    void EndBatch(const ReadBatch &, const EmitStepData &) override {}

    void Finish(bool, const EmitStepData &) override {}

   private:
    std::int64_t rank_;
};

// 占位实现：不上报任何指标。
class RankMetricExporter final : public MetricExporter
{
   public:
    void ExportStep(const StepDataOutput &) override {}
};

// 占位实现：不落盘任何 trace。
class RankTraceExporter final : public TraceExporter
{
   public:
    std::filesystem::path WriteStep(const StepDataOutput &) override { return {}; }

    void CompressStep(const std::filesystem::path &) override {}
};

}  // namespace msmonitor::process
