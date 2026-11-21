// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#pragma once

#include <map>
#include <vector>

#include "ProcessTypes.h"

namespace msmonitor::process {

// One reader owns one rank and one training process across all rotated files.
class JsonlReader {
  public:
    using LineConsumer = std::function<void(const JsonlLine &)>;

    explicit JsonlReader(const Options &options) : options_(options), processId_(options.processId) {}

    // Snapshot file sizes first, then drain that bounded snapshot using small chunks.
    ReadBatch Poll(const LineConsumer &consume);
    void Finish(bool inputComplete, const LineConsumer &consume);

  private:
    struct Cursor {
        std::uintmax_t offset{0};
        std::uintmax_t line{0};
        std::string pending;
    };

    struct FileSnapshot {
        std::filesystem::path file;
        std::uintmax_t size;
    };

    std::vector<FileSnapshot> SnapshotFiles();
    bool ReadAppendedBytes(const FileSnapshot &snapshot, Cursor &cursor, const LineConsumer &consume);
    void EmitCompleteLines(const std::filesystem::path &file, Cursor &cursor, const LineConsumer &consume);

    Options options_;
    std::optional<std::uint64_t> processId_;
    std::map<std::filesystem::path, Cursor> cursors_;
};
} // namespace msmonitor::process
