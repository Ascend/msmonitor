// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#include "StepDataAnalyser.h"

#include <glog/logging.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <deque>
#include <iterator>
#include <limits>
#include <map>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "StepAnalysis.h"

namespace msmonitor::process
{
namespace
{

std::optional<StepBoundary> DecodeDeviceStep(const StepEvent &event)
{
    const auto &data = event.data;
    if (data.value("sourceKind", "") != "Device" || data.value("domain", "") != "default")
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
        throw std::invalid_argument("invalid device step marker");
    }
    return StepBoundary{id, event.startNs, event.endNs};
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
    };

    struct PendingStep
    {
        StepBoundary boundary;
        std::uint64_t observedBatch;
        std::size_t quietBatches{0};
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
        if (publishedEnd_ && start < *publishedEnd_)
        {
            throw std::runtime_error("late record after step publication; increase quietBatches or use offline replay");
        }
        if (line.text.size() > options_.maxBufferedInputBytes - bufferedInputBytes_)
        {
            throw std::runtime_error("step input buffer budget exceeded; check marker scope and pending steps");
        }
        auto event = std::make_shared<StepEvent>(StepEvent{start, end, std::move(data)});
        if (kind == ProcessConstants::MarkerKind)
        {
            if (const auto boundary = decodeStep_(*event))
            {
                RecordStepBoundary(*boundary);
            }
        }
        // A point at UINT64_MAX cannot belong to a representable half-open step.
        const auto intervalEnd = end == start && end != std::numeric_limits<std::uint64_t>::max() ? end + 1 : end;
        batchIntervals_.emplace_back(start, intervalEnd);
        eventsSorted_ = eventsSorted_ && (events_.empty() || events_.back().event->startNs <= start);
        events_.push_back({std::move(event), line.text.size()});
        bufferedInputBytes_ += line.text.size();
    }

    void RecordStepBoundary(const StepBoundary &boundary)
    {
        if (boundary.endNs <= boundary.startNs || (publishedEnd_ && boundary.startNs < *publishedEnd_))
        {
            throw std::runtime_error("invalid or already published step boundary");
        }
        const auto found = stepStarts_.find(boundary.stepId);
        if (found != stepStarts_.end())
        {
            const auto &known = steps_.at({found->second, boundary.stepId}).boundary;
            if (known.startNs != boundary.startNs || known.endNs != boundary.endNs)
            {
                throw std::runtime_error("conflicting boundaries for the same step");
            }
            return;
        }
        // Markers already count toward the input byte budget; do not cap steps per read batch.
        // Device step ranges may overlap. Keep their original boundaries and shared events.
        steps_.emplace(std::make_pair(boundary.startNs, boundary.stepId), PendingStep{boundary, batchId_});
        stepStarts_.emplace(boundary.stepId, boundary.startNs);
    }

    // Merge this batch's timestamp coverage once; query each pending step in logarithmic time.
    void EndBatch(const ReadBatch &batch, const EmitStepData &emit)
    {
        SortEventsByStartTime();
        const bool canConfirmSteps = batch.hasData && !batch.hasPartialLine && !batchIntervals_.empty();
        batchIntervals_ = MergeIntervals(std::move(batchIntervals_));
        for (auto &[start, pending] : steps_)
        {
            const auto &boundary = pending.boundary;
            const auto found =
                std::lower_bound(batchIntervals_.begin(), batchIntervals_.end(), boundary.startNs,
                                 [](const Interval &interval, std::uint64_t time) { return interval.second <= time; });
            const bool overlapsStep = found != batchIntervals_.end() && found->first < boundary.endNs;
            const bool isQuiet = canConfirmSteps && batchId_ > pending.observedBatch && !overlapsStep;
            pending.quietBatches = isQuiet ? pending.quietBatches + 1 : 0;
        }
        while (!steps_.empty() && steps_.begin()->second.quietBatches >= options_.quietBatches)
        {
            PublishOldestStep(StepCompletion::QuietBatch, emit);
        }
        batchIntervals_.clear();
        ++batchId_;
    }

    void SortEventsByStartTime()
    {
        if (!eventsSorted_)
        {
            std::stable_sort(events_.begin(), events_.end(), [](const CachedEvent &left, const CachedEvent &right)
                             { return left.event->startNs < right.event->startNs; });
            eventsSorted_ = true;
        }
    }

    void PublishOldestStep(StepCompletion completion, const EmitStepData &emit)
    {
        const auto first = steps_.begin();
        const auto next = std::next(first);
        const auto retainFrom = next == steps_.end()
                                    ? first->second.boundary.endNs
                                    : std::min(first->second.boundary.endNs, next->second.boundary.startNs);
        BuildAndPublishStep(first->second.boundary, retainFrom, completion, emit);
        stepStarts_.erase(first->second.boundary.stepId);
        steps_.erase(first);
    }

    // Scan only events before this step's end. Keep events needed by later overlapping steps.
    void BuildAndPublishStep(const StepBoundary &boundary, std::uint64_t retainFrom, StepCompletion completion,
                             const EmitStepData &emit)
    {
        const auto analysisStart = std::chrono::steady_clock::now();
        auto output = std::make_shared<StepDataOutput>();
        output->boundary = boundary;
        output->rank = options_.rank;
        output->completion = completion;
        std::size_t unassigned = 0;
        const auto scanEnd = std::lower_bound(events_.begin(), events_.end(), boundary.endNs,
                                              [](const CachedEvent &cached, std::uint64_t time)
                                              { return cached.event->startNs < time; });
        const auto retained = std::remove_if(
            events_.begin(), scanEnd,
            [&](CachedEvent &cached)
            {
                const auto &event = *cached.event;
                if (Intersects(event, boundary))
                {
                    output->events.push_back(cached.event);
                    cached.assigned = true;
                    // The latest containing step owns a sample when device step ranges overlap.
                    auto owner = steps_.upper_bound({event.startNs, std::numeric_limits<std::uint64_t>::max()});
                    while (owner != steps_.begin())
                    {
                        --owner;
                        if (event.startNs < owner->second.boundary.endNs)
                        {
                            if (owner->second.boundary.stepId == boundary.stepId)
                            {
                                output->ownedEvents.push_back(cached.event);
                            }
                            break;
                        }
                    }
                }
                // Once this frontier is sealed, older unassigned records cannot form a later step.
                // Retain points at the end boundary and ranges crossing it for the next step.
                const bool consumed =
                    event.endNs < retainFrom || (event.endNs == retainFrom && event.startNs < event.endNs);
                if (consumed)
                {
                    bufferedInputBytes_ -= cached.inputBytes;
                    unassigned += !cached.assigned;
                }
                return consumed;
            });
        events_.erase(retained, scanEnd);
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
        publishedEnd_ = std::max(publishedEnd_.value_or(0), boundary.endNs);
        LOG(INFO) << "Process step analysis complete, rank: " << options_.rank << ", step: " << boundary.stepId
                  << ", events: " << eventCount << ", owned_events: " << ownedEventCount
                  << ", devices: " << deviceCount << ", analysis_us: " << analysisUs.count()
                  << ", completion: " << CompletionReason(completion);
    }

    Options options_;
    StepMarkerDecoder decodeStep_;
    std::function<void(const nlohmann::json &)> metadataSink_;
    // Retire consumed prefixes without moving every future event for each step.
    std::deque<CachedEvent> events_;
    std::map<std::pair<std::uint64_t, std::uint64_t>, PendingStep> steps_;
    std::unordered_map<std::uint64_t, std::uint64_t> stepStarts_;
    std::vector<Interval> batchIntervals_;
    std::optional<std::uint64_t> publishedEnd_;
    std::size_t bufferedInputBytes_{0};
    std::uint64_t batchId_{0};
    bool eventsSorted_{true};
};

StepDataAnalyser::StepDataAnalyser(Options options) : StepDataAnalyser(std::move(options), DecodeDeviceStep) {}

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
    impl_->SortEventsByStartTime();
    while (!impl_->steps_.empty())
    {
        impl_->PublishOldestStep(completion, emit);
    }
    const auto unassigned = std::count_if(impl_->events_.begin(), impl_->events_.end(),
                                          [](const auto &cached) { return !cached.assigned; });
    if (unassigned != 0)
    {
        LOG(WARNING) << "Process unassigned events at shutdown, rank: " << impl_->options_.rank
                     << ", count: " << unassigned;
    }
    impl_->events_.clear();
    impl_->bufferedInputBytes_ = 0;
    impl_->batchIntervals_.clear();
}

std::int64_t StepDataAnalyser::Rank() const { return impl_->options_.rank; }
}  // namespace msmonitor::process
