// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace msmonitor::process {

// Central defaults and file conventions; mutable runtime state belongs to its owner.
struct ProcessConstants final {
    static constexpr std::chrono::milliseconds PollInterval{1000};
    static constexpr std::size_t QueueCapacity{8};
    static constexpr std::size_t ReadChunkBytes{256 * 1024};
    static constexpr std::size_t MaxLineBytes{4 * 1024 * 1024};
    static constexpr std::int64_t InvalidRank{-1};
    static constexpr const char *FilePrefix{"msmonitor_"};
    static constexpr const char *FileExtension{".jsonl"};
};
} // namespace msmonitor::process
