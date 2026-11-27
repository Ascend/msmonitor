// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#include "StepTraceExporter.h"

#include <glog/logging.h>

#include <chrono>
#include <numeric>
#include <stdexcept>
#include <utility>

#include "TraceWriters.h"

namespace msmonitor::process
{
StepTraceExporter::StepTraceExporter(TraceOptions options) : options_(std::move(options))
{
    if (options_.outputDir.empty() || options_.stepsPerFile == 0 ||
        (options_.format == TraceFormat::MindStudioDb && options_.stepsPerFile != 1) ||
        (options_.format != TraceFormat::ChromeTrace && options_.format != TraceFormat::MindStudioDb))
    {
        throw std::invalid_argument("invalid trace options");
    }
    options_.outputDir = std::filesystem::absolute(options_.outputDir).lexically_normal();
}

void StepTraceExporter::WriteStep(StepDataPtr step)
{
    if (!step || step->rank < 0 || step->boundary.endNs <= step->boundary.startNs ||
        (!steps_.empty() && steps_.front()->rank != step->rank))
    {
        throw std::logic_error("invalid step for trace export");
    }
    steps_.push_back(std::move(step));
    if (steps_.size() == options_.stepsPerFile)
    {
        Flush();
    }
}

void StepTraceExporter::Flush()
{
    if (steps_.empty())
    {
        return;
    }
    const auto &first = *steps_.front();
    const auto rank = first.rank;
    const auto firstStep = first.boundary.stepId;
    const auto lastStep = steps_.back()->boundary.stepId;
    const auto stepCount = steps_.size();
    const auto eventCount = std::accumulate(steps_.begin(), steps_.end(), std::size_t{0},
        [](std::size_t count, const StepDataPtr &step) { return count + step->events.size(); });
    const auto traceStart = std::chrono::steady_clock::now();
    const auto parent = options_.outputDir / ("rank_" + std::to_string(first.rank));
    auto directoryName = "step_" + std::to_string(first.boundary.stepId);
    if (steps_.size() > 1)
    {
        directoryName += "_" + std::to_string(steps_.back()->boundary.stepId);
    }
    const auto directory = parent / directoryName;
    const auto staging = parent / ("." + directory.filename().string() + ".tmp");
    std::filesystem::create_directories(parent);
    if (std::filesystem::exists(directory) || !std::filesystem::create_directory(staging))
    {
        throw std::runtime_error("step trace output already exists: " + directory.string());
    }
    // Failures preserve the staging directory. Replays must explicitly choose a clean output root.
    const auto *fileName = options_.format == TraceFormat::ChromeTrace ? "trace.json" : "trace.db";
    if (options_.format == TraceFormat::ChromeTrace)
    {
        WriteChromeTrace(steps_, staging / fileName);
    }
    else
    {
        WriteMindStudioDb(first, staging / fileName);
    }
    std::filesystem::rename(staging, directory);
    const auto traceUs =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - traceStart);
    std::error_code sizeError;
    const auto outputBytes = std::filesystem::file_size(directory / fileName, sizeError);
    steps_.clear();
    if (options_.keepFiles != 0)
    {
        stepDirectories_.push_back(directory);
        if (stepDirectories_.size() > options_.keepFiles)
        {
            const auto &oldest = stepDirectories_.front();
            if (std::filesystem::is_directory(std::filesystem::symlink_status(oldest)))
            {
                std::filesystem::remove(oldest / fileName);
            }
            std::filesystem::remove(oldest);
            stepDirectories_.pop_front();
        }
    }
    LOG(INFO) << "Process trace file written, rank: " << rank << ", steps: " << firstStep << "-" << lastStep
              << ", step_count: " << stepCount << ", events: " << eventCount << ", format: "
              << (options_.format == TraceFormat::ChromeTrace ? "chrome_trace" : "mindstudio_db")
              << ", output_bytes: " << (sizeError ? "unknown" : std::to_string(outputBytes))
              << ", write_us: " << traceUs.count()
              << ", path: " << directory.string();
}
}  // namespace msmonitor::process
