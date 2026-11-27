// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#include "StepAnalysis.h"

#include <algorithm>
#include <iterator>

namespace msmonitor::process
{
Intervals MergeIntervals(Intervals intervals)
{
    if (!std::is_sorted(intervals.begin(), intervals.end()))
    {
        std::sort(intervals.begin(), intervals.end());
    }
    std::size_t count = 0;
    for (const auto &interval : intervals)
    {
        if (interval.second <= interval.first)
        {
            continue;
        }
        if (count != 0 && interval.first <= intervals[count - 1].second)
        {
            intervals[count - 1].second = std::max(intervals[count - 1].second, interval.second);
        }
        else
        {
            intervals[count++] = interval;
        }
    }
    intervals.resize(count);
    return intervals;
}

std::uint64_t IntervalLength(const Intervals &intervals)
{
    std::uint64_t length = 0;
    for (const auto &[start, end] : intervals)
    {
        length += end - start;
    }
    return length;
}

namespace
{
// Both inputs are sorted, disjoint unions. Emit communication's covered and uncovered parts.
void AnalyseCommunicationOverlap(DeviceAnalysis &device)
{
    auto firstComputing = device.computing.begin();
    for (const auto &[start, end] : device.communication)
    {
        auto cursor = start;
        while (firstComputing != device.computing.end() && firstComputing->second <= start)
        {
            ++firstComputing;
        }
        for (auto current = firstComputing; current != device.computing.end(); ++current)
        {
            const auto [cs, ce] = *current;
            if (ce <= cursor)
            {
                continue;
            }
            if (cs >= end)
            {
                break;
            }
            if (cs > cursor)
            {
                device.uncoveredCommunication.emplace_back(cursor, cs);
            }
            device.overlap.emplace_back(std::max(cursor, cs), std::min(end, ce));
            cursor = std::min(end, ce);
            if (cursor == end)
            {
                break;
            }
        }
        if (cursor < end)
        {
            device.uncoveredCommunication.emplace_back(cursor, end);
        }
    }
}

void AnalyseFreeIntervals(DeviceAnalysis &device, const StepBoundary &step)
{
    Intervals busy;
    busy.reserve(device.computing.size() + device.communication.size());
    std::merge(device.computing.begin(), device.computing.end(), device.communication.begin(),
               device.communication.end(), std::back_inserter(busy));
    auto cursor = step.startNs;
    for (const auto &[start, end] : MergeIntervals(std::move(busy)))
    {
        if (start > cursor)
        {
            device.free.emplace_back(cursor, start);
        }
        cursor = end;
    }
    if (cursor < step.endNs)
    {
        device.free.emplace_back(cursor, step.endNs);
    }
}
}  // namespace

std::shared_ptr<const StepAnalysis> AnalyseStepIntervals(const StepDataOutput &step)
{
    auto result = std::make_shared<StepAnalysis>();
    for (const auto &event : step.events)
    {
        const auto kind = event->data.value("kind", "");
        if ((kind != "Kernel" && kind != "Communication") || !event->data.contains("deviceId"))
        {
            continue;
        }
        auto &device = result->devices[event->data.at("deviceId").get<std::uint64_t>()];
        const Interval interval{std::max(event->startNs, step.boundary.startNs),
                                std::min(event->endNs, step.boundary.endNs)};
        // These are observed Kernel/Communication activity scopes, not hardware utilization.
        (kind == "Kernel" ? device.computing : device.communication).push_back(interval);
    }
    for (auto &[id, device] : result->devices)
    {
        device.computing = MergeIntervals(std::move(device.computing));
        device.communication = MergeIntervals(std::move(device.communication));
        AnalyseCommunicationOverlap(device);
        AnalyseFreeIntervals(device, step.boundary);
    }
    return result;
}
}  // namespace msmonitor::process
