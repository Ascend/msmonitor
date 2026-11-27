// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#pragma once

#include <deque>

#include "ProcessStages.h"

namespace msmonitor::process
{
enum class TraceFormat
{
    ChromeTrace,
    MindStudioDb
};

struct TraceOptions
{
    // The caller supplies a job/run-specific root. Exporters append rank and step.
    std::filesystem::path outputDir;
    TraceFormat format{TraceFormat::ChromeTrace};
    bool keepDirectory{false};
    std::size_t keepFiles{0};
};

// One instance per rank's trace worker. Files are closed before directory publication.
class StepTraceExporter final : public TraceExporter
{
   public:
    explicit StepTraceExporter(TraceOptions options);
    std::filesystem::path WriteStep(const StepDataOutput &step) override;
    void CompressStep(const std::filesystem::path &directory) override;

   private:
    TraceOptions options_;
    std::optional<std::filesystem::path> pending_;
    std::deque<std::filesystem::path> archives_;
};
}  // namespace msmonitor::process
