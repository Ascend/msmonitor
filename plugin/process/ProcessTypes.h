// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "ProcessConstants.h"

namespace msmonitor::process {

struct Options {
    std::filesystem::path inputDir;
    std::int64_t rank{ProcessConstants::InvalidRank};
    // Required only when the input directory contains multiple processes for this rank.
    std::optional<std::uint64_t> processId;
    std::chrono::milliseconds pollInterval{ProcessConstants::PollInterval};
    std::size_t queueCapacity{ProcessConstants::QueueCapacity};
    std::size_t readChunkBytes{ProcessConstants::ReadChunkBytes};
    std::size_t maxLineBytes{ProcessConstants::MaxLineBytes};
};

struct JsonlLine {
    std::filesystem::path file;
    std::uintmax_t lineNumber{0};
    std::string text;
};

struct ReadBatch {
    bool hasData{false};
    bool hasPartialLine{false};
};

struct StepBoundary {
    std::uint64_t stepId;
    std::uint64_t startNs;
    std::uint64_t endNs;
};

// Minimal step identity handed to both export workers.
struct StepDataOutput {
    StepBoundary boundary;
    std::int64_t rank;
};

using StepDataPtr = std::shared_ptr<const StepDataOutput>;
using EmitStepData = std::function<void(StepDataPtr)>;
} // namespace msmonitor::process
