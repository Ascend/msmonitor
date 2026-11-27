// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#pragma once

#include <map>
#include <utility>

#include "ProcessTypes.h"

namespace msmonitor::process
{
using Interval = std::pair<std::uint64_t, std::uint64_t>;
using Intervals = std::vector<Interval>;

struct DeviceAnalysis
{
    Intervals computing;
    Intervals communication;
    Intervals overlap;
    Intervals uncoveredCommunication;
    Intervals free;
};

// Shared by metrics and trace export. All ranges are clipped to the step.
struct StepAnalysis
{
    std::map<std::uint64_t, DeviceAnalysis> devices;
};

Intervals MergeIntervals(Intervals intervals);
std::uint64_t IntervalLength(const Intervals &intervals);
std::shared_ptr<const StepAnalysis> AnalyseStepIntervals(const StepDataOutput &step);
}  // namespace msmonitor::process
