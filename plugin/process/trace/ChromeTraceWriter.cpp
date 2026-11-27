// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#include <algorithm>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <unordered_set>

#include "TraceWriters.h"

namespace msmonitor::process
{
namespace
{
struct ChromeConstants final
{
    static constexpr double NsPerUs{1000.0};
};
}  // namespace

void WriteChromeTrace(const std::vector<StepDataPtr> &steps, const std::filesystem::path &file)
{
    std::ofstream output;
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output.open(file, std::ios::binary);
    output << "{\"traceEvents\":[";
    bool first = true;
    const auto emit = [&](const nlohmann::json &event, const nlohmann::json *args = nullptr)
    {
        if (!first)
        {
            output << ',';
        }
        first = false;
        if (args == nullptr)
        {
            output << event;
            return;
        }
        // Stream the input record directly instead of cloning it into the trace event.
        auto prefix = event.dump();
        prefix.back() = ',';
        output << prefix << "\"args\":" << *args << '}';
    };
    // Keep integer epoch nanoseconds out of floating-point trace timestamps.
    auto base = steps.front()->boundary.startNs;
    for (const auto &step : steps)
    {
        base = std::min(base, step->boundary.startNs);
        for (const auto &event : step->events)
        {
            base = std::min(base, event->startNs);
        }
    }
    const auto rank = steps.front()->rank;
    using TrackKey = std::tuple<std::string, std::uint64_t>;
    std::map<TrackKey, std::uint64_t> tracks;
    const auto track = [&](const std::string &kind, std::uint64_t id)
    {
        const auto [position, inserted] = tracks.emplace(TrackKey{kind, id}, tracks.size() + 1);
        const auto pid = position->second;
        if (inserted)
        {
            emit({{"ph", "M"},
                  {"name", "process_name"},
                  {"pid", pid},
                  {"tid", 0},
                  {"args", {{"name", "Rank " + std::to_string(rank) + " / " + kind + " " + std::to_string(id)}}}});
        }
        return pid;
    };
    const auto interval = [&](const std::string &name, const std::string &category, std::uint64_t pid,
                              std::uint64_t tid, std::uint64_t start, std::uint64_t end, const nlohmann::json &args)
    {
        nlohmann::json event{{"ph", start == end ? "i" : "X"},
                             {"name", name},
                             {"cat", category},
                             {"pid", pid},
                             {"tid", tid},
                             {"ts", (start - base) / ChromeConstants::NsPerUs}};
        if (start == end)
        {
            event["s"] = "t";
        }
        else
        {
            event["dur"] = (end - start) / ChromeConstants::NsPerUs;
        }
        emit(event, &args);
    };
    std::set<std::pair<std::uint64_t, std::uint64_t>> namedTracks;
    std::unordered_set<const StepEvent *> crossingEvents;
    auto metadata = nlohmann::json::array();
    for (std::size_t index = 0; index < steps.size(); ++index)
    {
        const auto &step = *steps[index];
        const auto nextStart = index + 1 < steps.size() ? steps[index + 1]->boundary.startNs
                                                        : std::numeric_limits<std::uint64_t>::max();
        interval("step " + std::to_string(step.boundary.stepId), "Step", track("Step", 0), 0, step.boundary.startNs,
                 step.boundary.endNs, {{"completion", static_cast<int>(step.completion)}});
        for (const auto &event : step.events)
        {
            // A crossing event can be shared by multiple steps in the same trace file.
            if (crossingEvents.count(event.get()) != 0)
            {
                continue;
            }
            if (event->endNs >= nextStart)
            {
                crossingEvents.insert(event.get());
            }
            const auto &data = event->data;
            const auto kind = data.value("kind", "unknown");
            const bool device = data.contains("deviceId") && data.value("sourceKind", "Device") != "Host";
            const auto pid = device ? track("Device", data.at("deviceId").get<std::uint64_t>())
                                    : track("Host", data.value("processId", std::uint64_t{0}));
            const auto tid = data.value(device ? "streamId" : "threadId", std::uint64_t{0});
            interval(data.value("name", kind), kind, pid, tid, event->startNs, event->endNs, data);
        }
        const auto analysis = step.analysis ? step.analysis : AnalyseStepIntervals(step);
        for (const auto &[device, ranges] : analysis->devices)
        {
            const auto pid = track("Overlap Analysis / Device", device);
            std::uint64_t tid = 0;
            for (const auto &[name, intervals] : std::vector<std::pair<const char *, const Intervals *>>{
                     {"Computing", &ranges.computing},
                     {"Communication", &ranges.communication},
                     {"Overlap", &ranges.overlap},
                     {"Uncovered Communication", &ranges.uncoveredCommunication},
                     {"Observed Free", &ranges.free}})
            {
                if (namedTracks.emplace(pid, tid).second)
                {
                    emit({{"ph", "M"}, {"name", "thread_name"}, {"pid", pid}, {"tid", tid}, {"args", {{"name", name}}}});
                }
                for (const auto &[start, end] : *intervals)
                {
                    interval(name, "StepAnalysis", pid, tid, start, end, {{"deviceId", device}});
                }
                ++tid;
            }
        }
        metadata.push_back({{"stepId", step.boundary.stepId},
                            {"startNs", step.boundary.startNs},
                            {"endNs", step.boundary.endNs}});
    }
    nlohmann::json traceMetadata{{"baseNs", base}, {"rank", rank}, {"steps", metadata}};
    if (steps.size() == 1)
    {
        traceMetadata.update(metadata.front());
    }
    output << "],\"displayTimeUnit\":\"ms\",\"metadata\":" << traceMetadata << '}';
    output.close();
}
}  // namespace msmonitor::process
