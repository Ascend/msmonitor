// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#pragma once

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include "ProcessConstants.h"

// 内部实现细节，仅为单元测试可测而单独成头，不属于对外接口。
namespace msmonitor::process_runner::detail
{

// 一个 rank 当前最新的数据文件
struct RankFile
{
    std::int64_t rank{process::ProcessConstants::Unranked};
    std::uint64_t processId{0};
    // 文件名里的时间戳是 17 位定宽，可直接比较，省掉逐文件 stat
    std::uint64_t timestamp{0};
    std::filesystem::path path;
};

// 文件名约定 msmonitor_<pid>_<timestamp>_<rank>.jsonl，rank 取最后一个 '_' 之后。
inline bool ParseRankFileName(const std::filesystem::path &path, RankFile &rankFile)
{
    const std::string name = path.filename().string();
    // 用 string_view 绑定字符串常量，避免每个目录条目各构造一次 std::string
    constexpr std::string_view prefix{process::ProcessConstants::FilePrefix};
    constexpr std::string_view suffix{process::ProcessConstants::FileExtension};
    if (name.size() <= prefix.size() + suffix.size() || name.compare(0, prefix.size(), prefix) != 0 ||
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
    {
        return false;
    }

    // 只在 name 上开视图，不再复制文件名主体
    const std::string_view body(name.data() + prefix.size(), name.size() - prefix.size() - suffix.size());
    const auto first = body.find('_');
    const auto last = body.rfind('_');
    if (first == std::string_view::npos || first == last)
    {
        return false;
    }

    std::uint64_t processId = 0;
    auto pidResult = std::from_chars(body.data(), body.data() + first, processId);
    if (pidResult.ec != std::errc() || pidResult.ptr != body.data() + first || processId == 0)
    {
        return false;
    }

    std::uint64_t timestamp = 0;
    auto stampResult = std::from_chars(body.data() + first + 1, body.data() + last, timestamp);
    if (stampResult.ec != std::errc() || stampResult.ptr != body.data() + last)
    {
        // 时间戳缺失或非数字时不丢文件，退化为最小值参与比较
        timestamp = 0;
    }

    std::int64_t rank = process::ProcessConstants::Unranked;
    auto rankResult = std::from_chars(body.data() + last + 1, body.data() + body.size(), rank);
    if (rankResult.ec != std::errc() || rankResult.ptr != body.data() + body.size() || rank < 0)
    {
        return false;
    }

    rankFile = RankFile{rank, processId, timestamp, path};
    return true;
}

}  // namespace msmonitor::process_runner::detail
