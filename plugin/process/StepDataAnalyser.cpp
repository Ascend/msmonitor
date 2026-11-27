// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#include "StepDataAnalyser.h"

#include <glog/logging.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <deque>
#include <limits>
#include <map>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "StepAnalysis.h"

namespace msmonitor::process
{
namespace
{

enum EventSide : std::size_t
{
    Device,
    Host,
    SideCount
};

EventSide GetEventSide(const nlohmann::json &data)
{
    const auto source = data.find("sourceKind");
    if (source == data.end())
    {
        return data.contains("deviceId") ? Device : Host;
    }
    return *source == "Device" ? Device : Host;
}

std::optional<StepBoundary> DecodeStepMarker(const StepEvent &event)
{
    const auto &data = event.data;
    const auto source = data.value("sourceKind", "");
    if ((source != "Device" && source != "Host") || data.value("domain", "") != "default")
    {
        return {};
    }
    const auto name = data.value("name", "");
    constexpr std::string_view prefix{"step "};
    if (name.rfind(prefix, 0) != 0)
    {
        return {};
    }
    std::uint64_t id{};
    const auto end = name.data() + name.size();
    const auto parsed = std::from_chars(name.data() + prefix.size(), end, id);
    if (parsed.ec != std::errc{} || parsed.ptr != end)
    {
        return {};
    }
    return StepBoundary{id, event.startNs, event.endNs};
}

std::optional<std::uint64_t> MarkerId(const StepEventPtr &event)
{
    const auto id = event->data.find("id");
    return id != event->data.end() && id->is_number_unsigned() ? std::optional{id->get<std::uint64_t>()} : std::nullopt;
}

std::uint64_t ReadTimestamp(const nlohmann::json &data, const char *key)
{
    const auto &value = data.at(key);
    if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 0))
    {
        throw std::runtime_error(std::string("invalid timestamp: ") + key);
    }
    return value.get<std::uint64_t>();
}

// Treat instantaneous markers as points in a half-open step interval.
bool Intersects(const StepEvent &event, const StepBoundary &step)
{
    return event.startNs < step.endNs &&
           (event.endNs > step.startNs || (event.startNs == event.endNs && event.startNs >= step.startNs));
}

const char *CompletionReason(StepCompletion completion)
{
    switch (completion)
    {
        case StepCompletion::QuietBatch:
            return "quiet batch";
        case StepCompletion::EndOfInput:
            return "end of input";
        case StepCompletion::Stopped:
            return "stop requested";
    }
    return "unknown";
}
}  // namespace

struct StepDataAnalyser::Impl
{
    struct CachedEvent
    {
        StepEventPtr event;
        std::size_t inputBytes;
        bool assigned{false};
        bool owned{false};
    };

    using Boundaries = std::map<std::pair<std::uint64_t, std::uint64_t>, StepBoundary>;

    struct Lane
    {
        std::deque<CachedEvent> events;
        std::vector<Interval> batchIntervals;
        std::optional<std::uint64_t> publishedEnd;
        bool sorted{true};
    };

    struct StepMarker
    {
        StepBoundary boundary;
        StepEventPtr event;
        std::size_t inputBytes;
    };

    struct PendingStep
    {
        std::array<std::optional<StepMarker>, SideCount> markers;
        std::uint64_t observedBatch{0};
        std::size_t quietBatches{0};

        const StepBoundary &Boundary(std::size_t side) const
        {
            return markers[side] ? markers[side]->boundary : markers[side == Host ? Device : Host]->boundary;
        }
    };

    Impl(Options config, StepMarkerDecoder decoder, std::function<void(const nlohmann::json &)> metadataSink)
        : options_(std::move(config)), decodeStep_(std::move(decoder)), metadataSink_(std::move(metadataSink))
    {
    }

    void ProcessLine(const JsonlLine &line)
    {
        if (line.text.find_first_not_of(" \t\r\n") == std::string::npos)
        {
            return;
        }
        auto data = nlohmann::json::parse(line.text);
        const auto kind = data.at(ProcessConstants::KindKey).get<std::string>();
        if (kind == ProcessConstants::RankDeviceMapKind || kind == ProcessConstants::ParallelGroupInfoKind)
        {
            if (metadataSink_)
            {
                metadataSink_(data);
            }
            VLOG(1) << "Process received non-timed metadata, kind: " << kind << ", rank: " << options_.rank;
            return;
        }
        const auto start = ReadTimestamp(data, ProcessConstants::StartNsKey);
        const auto end = ReadTimestamp(data, ProcessConstants::EndNsKey);
        if (end < start)
        {
            throw std::runtime_error("event end precedes start");
        }
        StepEvent event{start, end, std::move(data)};
        const auto boundary = kind == ProcessConstants::MarkerKind ? decodeStep_(event) : std::nullopt;
        const auto side = GetEventSide(event.data);
        auto &lane = lanes_[side];
        // Step markers identify their own step. Other ranges may cross a sealed interval.
        const bool late = boundary ? (publishedStep_ && boundary->stepId <= *publishedStep_)
                                   : (lane.publishedEnd && start < *lane.publishedEnd && end <= *lane.publishedEnd);
        if (late)
        {
            LOG(ERROR) << "Process skipped late record, rank: " << options_.rank << ", file: " << line.file
                       << ", line: " << line.lineNumber << ", kind: " << kind << ", start_ns: " << start
                       << ", end_ns: " << end;
            return;
        }
        if (line.text.size() > options_.maxBufferedInputBytes - bufferedInputBytes_)
        {
            throw std::runtime_error("step input buffer budget exceeded; check marker scope and pending steps");
        }
        auto record = std::make_shared<StepEvent>(std::move(event));
        if (boundary)
        {
            if (!RecordStepMarker(side, {*boundary, std::move(record), line.text.size()}))
            {
                return;
            }
        }
        else
        {
            lane.sorted = lane.sorted && (lane.events.empty() || lane.events.back().event->startNs <= start);
            lane.events.push_back({std::move(record), line.text.size()});
        }
        // A point at UINT64_MAX cannot belong to a representable half-open step.
        const auto intervalEnd = end == start && end != std::numeric_limits<std::uint64_t>::max() ? end + 1 : end;
        lane.batchIntervals.emplace_back(start, intervalEnd);
        bufferedInputBytes_ += line.text.size();
    }

    bool RecordStepMarker(EventSide side, StepMarker marker)
    {
        const auto &boundary = marker.boundary;
        if (boundary.endNs <= boundary.startNs)
        {
            LOG(ERROR) << "Process skipped invalid step marker, rank: " << options_.rank
                       << ", step: " << boundary.stepId;
            return false;
        }
        auto &step = steps_[boundary.stepId];
        auto &current = step.markers[side];
        if (current)
        {
            if (current->boundary.startNs != boundary.startNs || current->boundary.endNs != boundary.endNs ||
                MarkerId(current->event) != MarkerId(marker.event))
            {
                LOG(ERROR) << "Process skipped conflicting step marker, rank: " << options_.rank
                           << ", step: " << boundary.stepId << ", side: " << (side == Host ? "Host" : "Device");
            }
            return false;
        }
        const auto &other = step.markers[side == Host ? Device : Host];
        if (other)
        {
            const auto markerId = MarkerId(marker.event);
            const auto otherId = MarkerId(other->event);
            if (markerId && otherId && markerId != otherId)
            {
                LOG(ERROR) << "Process step marker IDs disagree, rank: " << options_.rank
                           << ", step: " << boundary.stepId << ", ignoring Host marker";
                if (side == Host)
                {
                    return false;
                }
                bufferedInputBytes_ -= other->inputBytes;
                step.markers[Host].reset();
            }
        }
        current = std::move(marker);
        step.observedBatch = batchId_;
        step.quietBatches = 0;
        return true;
    }

    void EndBatch(const ReadBatch &batch, const EmitStepData &emit)
    {
        bool hasTimedData = false;
        for (auto &lane : lanes_)
        {
            lane.batchIntervals = MergeIntervals(std::move(lane.batchIntervals));
            hasTimedData |= !lane.batchIntervals.empty();
        }
        if (batch.hasPartialLine || (batch.hasData && hasTimedData))
        {
            for (auto &[id, step] : steps_)
            {
                bool quiet = !batch.hasPartialLine && batchId_ > step.observedBatch;
                for (std::size_t side = 0; side < SideCount && quiet; ++side)
                {
                    const auto &boundary = step.Boundary(side);
                    const auto &intervals = lanes_[side].batchIntervals;
                    const auto found = std::lower_bound(intervals.begin(), intervals.end(), boundary.startNs,
                                                        [](const Interval &interval, std::uint64_t time)
                                                        { return interval.second <= time; });
                    quiet = found == intervals.end() || found->first >= boundary.endNs;
                }
                step.quietBatches = quiet ? step.quietBatches + 1 : 0;
            }
        }
        PublishSteps(StepCompletion::QuietBatch, emit);
        for (auto &lane : lanes_)
        {
            lane.batchIntervals.clear();
        }
        ++batchId_;
    }

    bool IsReady(const PendingStep &step) const
    {
        // Give an identified marker one more quiet batch to receive its peer.
        if (step.quietBatches < options_.quietBatches)
        {
            return false;
        }
        if (step.markers[Device] && step.markers[Host])
        {
            return true;
        }
        const auto &marker = step.markers[Device] ? *step.markers[Device] : *step.markers[Host];
        return !MarkerId(marker.event) || step.quietBatches > options_.quietBatches;
    }

    void PublishSteps(StepCompletion completion, const EmitStepData &emit)
    {
        const auto ready = [&](const PendingStep &step)
        { return completion != StepCompletion::QuietBatch || IsReady(step); };
        if (steps_.empty() || !ready(steps_.begin()->second))
        {
            return;
        }
        std::array<Boundaries, SideCount> boundaries;
        for (std::size_t side = 0; side < SideCount; ++side)
        {
            auto &lane = lanes_[side];
            if (!lane.sorted)
            {
                std::stable_sort(lane.events.begin(), lane.events.end(),
                                 [](const CachedEvent &left, const CachedEvent &right)
                                 { return left.event->startNs < right.event->startNs; });
                lane.sorted = true;
            }
            for (const auto &[id, step] : steps_)
            {
                const auto &boundary = step.Boundary(side);
                boundaries[side].emplace(std::make_pair(boundary.startNs, id), boundary);
            }
        }

        while (!steps_.empty() && ready(steps_.begin()->second))
        {
            const auto first = steps_.begin();
            BuildAndPublishStep(first->second, boundaries, completion, emit);
            publishedStep_ = first->first;
            steps_.erase(first);
        }
    }

    // Share crossing ranges with trace, and give each metric sample a single owner.
    std::size_t CollectLaneEvents(Lane &lane, const Boundaries &boundaries, const StepBoundary &boundary,
                                  StepDataOutput &output)
    {
        std::size_t unassigned = 0;
        const auto scanEnd = std::lower_bound(lane.events.begin(), lane.events.end(), boundary.endNs,
                                              [](const CachedEvent &cached, std::uint64_t time)
                                              { return cached.event->startNs < time; });
        const auto retainFrom = [&]
        {
            auto next = boundaries.begin();
            if (next->first == std::make_pair(boundary.startNs, boundary.stepId))
            {
                ++next;
            }
            return next == boundaries.end() ? boundary.endNs : std::min(boundary.endNs, next->first.first);
        }();
        const auto retained = std::remove_if(
            lane.events.begin(), scanEnd,
            [&](CachedEvent &cached)
            {
                const auto &event = *cached.event;
                if (Intersects(event, boundary))
                {
                    output.events.push_back(cached.event);
                    cached.assigned = true;
                    auto owner =
                        cached.owned
                            ? boundaries.begin()
                            : boundaries.upper_bound({event.startNs, std::numeric_limits<std::uint64_t>::max()});
                    while (owner != boundaries.begin())
                    {
                        --owner;
                        if (event.startNs < owner->second.endNs)
                        {
                            if (owner->second.stepId == boundary.stepId)
                            {
                                output.ownedEvents.push_back(cached.event);
                                cached.owned = true;
                            }
                            break;
                        }
                    }
                }
                // Retain points at the end boundary and ranges crossing it.
                const bool consumed =
                    event.endNs < retainFrom || (event.endNs == retainFrom && event.startNs < event.endNs);
                if (consumed)
                {
                    bufferedInputBytes_ -= cached.inputBytes;
                    unassigned += !cached.assigned;
                }
                return consumed;
            });
        lane.events.erase(retained, scanEnd);
        lane.publishedEnd = std::max(lane.publishedEnd.value_or(0), retainFrom);
        return unassigned;
    }

    void BuildAndPublishStep(const PendingStep &step, std::array<Boundaries, SideCount> &boundaries,
                             StepCompletion completion, const EmitStepData &emit)
    {
        const auto analysisStart = std::chrono::steady_clock::now();
        auto output = std::make_shared<StepDataOutput>();
        const auto &boundary = step.Boundary(Device);
        output->boundary = boundary;
        output->rank = options_.rank;
        output->completion = completion;
        if (step.markers[Host])
        {
            output->hostBoundary = step.markers[Host]->boundary;
        }
        std::size_t unassigned = 0;
        for (std::size_t side = 0; side < SideCount; ++side)
        {
            const auto &sideBoundary = step.Boundary(side);
            unassigned += CollectLaneEvents(lanes_[side], boundaries[side], sideBoundary, *output);
            boundaries[side].erase({sideBoundary.startNs, sideBoundary.stepId});
            if (const auto &marker = step.markers[side])
            {
                output->events.push_back(marker->event);
                bufferedInputBytes_ -= marker->inputBytes;
            }
        }
        if (unassigned != 0)
        {
            LOG(WARNING) << "Process events outside known steps before sealed boundary, rank: " << options_.rank
                         << ", step: " << boundary.stepId << ", count: " << unassigned;
        }
        output->analysis = AnalyseStepIntervals(*output);
        const auto analysisUs =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - analysisStart);
        const auto eventCount = output->events.size();
        const auto ownedEventCount = output->ownedEvents.size();
        const auto deviceCount = output->analysis->devices.size();
        emit(std::move(output));
        LOG(INFO) << "Process step analysis complete, rank: " << options_.rank << ", step: " << boundary.stepId
                  << ", events: " << eventCount << ", owned_events: " << ownedEventCount << ", devices: " << deviceCount
                  << ", analysis_us: " << analysisUs.count() << ", completion: " << CompletionReason(completion);
    }

    Options options_;
    StepMarkerDecoder decodeStep_;
    std::function<void(const nlohmann::json &)> metadataSink_;
    std::array<Lane, SideCount> lanes_;
    std::map<std::uint64_t, PendingStep> steps_;
    std::optional<std::uint64_t> publishedStep_;
    std::size_t bufferedInputBytes_{0};
    std::uint64_t batchId_{0};
};

StepDataAnalyser::StepDataAnalyser(Options options) : StepDataAnalyser(std::move(options), DecodeStepMarker) {}

StepDataAnalyser::StepDataAnalyser(Options options, StepMarkerDecoder decodeStep,
                                   std::function<void(const nlohmann::json &)> metadataSink)
{
    if (options.rank < ProcessConstants::Unranked || !options.maxBufferedInputBytes || !options.quietBatches ||
        !decodeStep)
    {
        throw std::invalid_argument("invalid analyser options or step marker decoder");
    }
    impl_ = std::make_unique<Impl>(std::move(options), std::move(decodeStep), std::move(metadataSink));
}

StepDataAnalyser::~StepDataAnalyser() = default;

void StepDataAnalyser::ProcessLine(const JsonlLine &line) { impl_->ProcessLine(line); }

void StepDataAnalyser::EndBatch(const ReadBatch &batch, const EmitStepData &emit) { impl_->EndBatch(batch, emit); }

void StepDataAnalyser::Finish(bool inputComplete, const EmitStepData &emit)
{
    const auto completion = inputComplete ? StepCompletion::EndOfInput : StepCompletion::Stopped;
    impl_->PublishSteps(completion, emit);
    std::size_t unassigned = 0;
    for (auto &lane : impl_->lanes_)
    {
        unassigned +=
            std::count_if(lane.events.begin(), lane.events.end(), [](const auto &cached) { return !cached.assigned; });
        lane.events.clear();
        lane.batchIntervals.clear();
    }
    if (unassigned != 0)
    {
        LOG(WARNING) << "Process unassigned events at shutdown, rank: " << impl_->options_.rank
                     << ", count: " << unassigned;
    }
    impl_->bufferedInputBytes_ = 0;
}

std::int64_t StepDataAnalyser::Rank() const { return impl_->options_.rank; }
}  // namespace msmonitor::process
