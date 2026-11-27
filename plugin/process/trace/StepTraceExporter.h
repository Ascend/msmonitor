// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#pragma once

#include <deque>
#include <filesystem>
#include <vector>

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
    std::size_t keepFiles{0};
    std::size_t stepsPerFile{1};
};

// One instance per rank's trace worker. Files are closed before directory publication.
class StepTraceExporter final : public TraceExporter
{
   public:
    explicit StepTraceExporter(TraceOptions options);
    void WriteStep(StepDataPtr step) override;
    void Flush() override;

   private:
    TraceOptions options_;
    std::vector<StepDataPtr> steps_;
    std::deque<std::filesystem::path> stepDirectories_;
};
}  // namespace msmonitor::process
