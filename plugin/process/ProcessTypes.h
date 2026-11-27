// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "ProcessConstants.h"

namespace msmonitor::process
{

struct Options
{
    std::filesystem::path inputDir;
    std::int64_t rank{ProcessConstants::UnspecifiedRank};
    // Required for unranked files or to disambiguate multiple processes for a rank.
    std::optional<std::uint64_t> processId;
    std::chrono::milliseconds pollInterval{ProcessConstants::PollInterval};
    std::size_t queueCapacity{ProcessConstants::QueueCapacity};
    std::size_t readChunkBytes{ProcessConstants::ReadChunkBytes};
    std::size_t maxLineBytes{ProcessConstants::MaxLineBytes};
    // Budget for serialized records retained by the analyser.
    std::size_t maxBufferedInputBytes{ProcessConstants::MaxBufferedInputBytes};
    std::size_t quietBatches{ProcessConstants::QuietBatches};
    bool recursiveInput{true};
};

struct JsonlLine
{
    std::filesystem::path file;
    std::uintmax_t lineNumber{0};
    std::string text;
};

struct ReadBatch
{
    bool hasData{false};
    bool hasPartialLine{false};
};

struct StepBoundary
{
    std::uint64_t stepId;
    std::uint64_t startNs;
    std::uint64_t endNs;
};

struct StepEvent
{
    std::uint64_t startNs;
    std::uint64_t endNs;
    nlohmann::json data;
};

using StepEventPtr = std::shared_ptr<const StepEvent>;
using StepMarkerDecoder = std::function<std::optional<StepBoundary>(const StepEvent &)>;

enum class StepCompletion
{
    QuietBatch,
    EndOfInput,
    Stopped
};
struct StepAnalysis;

struct StepDataOutput
{
    StepBoundary boundary;
    std::int64_t rank;
    StepCompletion completion{StepCompletion::Stopped};
    std::vector<StepEventPtr> events;
    std::vector<StepEventPtr> ownedEvents;
    std::shared_ptr<const StepAnalysis> analysis;
};

using StepDataPtr = std::shared_ptr<const StepDataOutput>;
using EmitStepData = std::function<void(StepDataPtr)>;
}  // namespace msmonitor::process
