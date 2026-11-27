// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#include "JsonlReader.h"

#include <fcntl.h>
#include <glog/logging.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace msmonitor::process
{
namespace
{
struct FileIdentity
{
    std::uint64_t processId;
    std::int64_t rank;
};

struct InputFile
{
    int fd;
    ~InputFile()
    {
        if (fd >= 0)
        {
            close(fd);
        }
    }
};

// Resolve each component below the configured root without following symlinks.
int OpenInputFile(const std::filesystem::path &root, const std::filesystem::path &file)
{
    InputFile directory{open(root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
    if (directory.fd < 0)
    {
        throw std::system_error(errno, std::generic_category(), root.string());
    }
    const auto relative = file.lexically_relative(root);
    auto component = relative.begin();
    const auto leaf = std::prev(relative.end());
    for (; component != leaf; ++component)
    {
        const int next = openat(directory.fd, component->c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0 && errno == ENOENT)
        {
            return -1;
        }
        if (next < 0)
        {
            throw std::system_error(errno, std::generic_category(), file.string());
        }
        close(directory.fd);
        directory.fd = next;
    }
    const int fd = openat(directory.fd, leaf->c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0 && errno != ENOENT)
    {
        throw std::system_error(errno, std::generic_category(), file.string());
    }
    return fd;
}

// Match the collector filename: msmonitor_<pid>_<timestamp>_<rank>.jsonl.
std::optional<FileIdentity> ParseFileIdentity(const std::filesystem::path &path)
{
    const auto stem = path.stem().string();
    const auto pidBegin = std::char_traits<char>::length(ProcessConstants::FilePrefix);
    const auto pidEnd = stem.find('_', pidBegin);
    const auto rankSeparator = stem.rfind('_');
    if (pidEnd == std::string::npos || rankSeparator <= pidEnd)
    {
        return {};
    }
    std::uint64_t pid{0};
    std::int64_t rank{0};
    const auto pidResult = std::from_chars(stem.data() + pidBegin, stem.data() + pidEnd, pid);
    const auto rankResult = std::from_chars(stem.data() + rankSeparator + 1, stem.data() + stem.size(), rank);
    if (pidResult.ec != std::errc{} || pidResult.ptr != stem.data() + pidEnd || rankResult.ec != std::errc{} ||
        rankResult.ptr != stem.data() + stem.size())
    {
        return {};
    }
    return FileIdentity{pid, rank};
}
}  // namespace

ReadBatch JsonlReader::Poll(const LineConsumer &consume)
{
    const auto files = SnapshotFiles();
    ReadBatch batch;
    for (const auto &snapshot : files)
    {
        auto [position, inserted] = cursors_.try_emplace(snapshot.file);
        if (inserted)
        {
            VLOG(1) << "Process discovered JSONL, rank: " << options_.rank << ", file: " << snapshot.file.string();
        }
        auto &cursor = position->second;
        batch.hasData = batch.hasData || snapshot.size > cursor.offset;
        if (!ReadAppendedBytes(snapshot, cursor, consume))
        {
            batch.hasPartialLine = true;
            cursors_.erase(position);
        }
    }
    auto snapshot = files.begin();
    for (auto cursor = cursors_.begin(); cursor != cursors_.end();)
    {
        const auto &[file, state] = *cursor;
        while (snapshot != files.end() && snapshot->file < file)
        {
            ++snapshot;
        }
        batch.hasPartialLine |= !state.pending.empty();
        if ((snapshot == files.end() || snapshot->file != file) && !std::filesystem::exists(file))
        {
            if (!state.pending.empty())
            {
                LOG(WARNING) << "Process JSONL removed with a partial line, rank: " << options_.rank
                             << ", file: " << file;
            }
            cursor = cursors_.erase(cursor);
        }
        else
        {
            ++cursor;
        }
    }
    return batch;
}

std::vector<JsonlReader::FileSnapshot> JsonlReader::SnapshotFiles()
{
    std::vector<FileSnapshot> files;
    const auto snapshotFile = [&](const std::filesystem::directory_entry &entry)
    {
        const auto name = entry.path().filename().string();
        if (name.rfind(ProcessConstants::FilePrefix, 0) != 0 ||
            entry.path().extension() != ProcessConstants::FileExtension)
        {
            return;
        }
        const auto identity = ParseFileIdentity(entry.path());
        if (!identity || identity->rank != options_.rank ||
            (options_.processId && identity->processId != *options_.processId))
        {
            return;
        }
        std::uintmax_t size;
        try
        {
            if (!std::filesystem::is_regular_file(entry.symlink_status()))
            {
                return;
            }
            size = entry.file_size();
        }
        catch (const std::filesystem::filesystem_error &error)
        {
            if (error.code() != std::errc::no_such_file_or_directory)
            {
                throw;
            }
            LOG(WARNING) << "Process JSONL removed during scan, rank: " << options_.rank << ", file: " << entry.path();
            return;
        }
        if (processId_ && identity->processId != *processId_)
        {
            throw std::runtime_error("JSONL belongs to a different process: " + entry.path().string() +
                                     "; set Options.processId or isolate the input directory");
        }
        processId_ = identity->processId;
        files.push_back({entry.path(), size});
    };
    if (options_.recursiveInput)
    {
        for (const auto &entry : std::filesystem::recursive_directory_iterator(options_.inputDir))
        {
            snapshotFile(entry);
        }
    }
    else
    {
        for (const auto &entry : std::filesystem::directory_iterator(options_.inputDir))
        {
            snapshotFile(entry);
        }
    }
    std::sort(files.begin(), files.end(), [](const auto &left, const auto &right) { return left.file < right.file; });
    return files;
}

bool JsonlReader::ReadAppendedBytes(const FileSnapshot &snapshot, Cursor &cursor, const LineConsumer &consume)
{
    if (snapshot.size < cursor.offset)
    {
        throw std::runtime_error("JSONL truncated: " + snapshot.file.string());
    }
    if (snapshot.size == cursor.offset)
    {
        return true;
    }
    const InputFile file{OpenInputFile(options_.inputDir, snapshot.file)};
    if (file.fd < 0)
    {
        LOG(WARNING) << "Process JSONL removed before read, rank: " << options_.rank << ", file: " << snapshot.file
                     << ", unread bytes in snapshot: " << snapshot.size - cursor.offset;
        return false;
    }
    struct stat status
    {
    };
    if (fstat(file.fd, &status) != 0 || !S_ISREG(status.st_mode) ||
        static_cast<std::uintmax_t>(status.st_size) < snapshot.size)
    {
        throw std::runtime_error("JSONL snapshot changed before read: " + snapshot.file.string());
    }
    std::string chunk;
    chunk.reserve(options_.readChunkBytes);
    // Do not chase appends beyond this snapshot or load the whole batch into memory.
    while (cursor.offset < snapshot.size)
    {
        const auto bytes =
            static_cast<std::size_t>(std::min<std::uintmax_t>(snapshot.size - cursor.offset, options_.readChunkBytes));
        chunk.resize(bytes);
        ssize_t count;
        do
        {
            count = pread(file.fd, chunk.data(), bytes, static_cast<off_t>(cursor.offset));
        } while (count < 0 && errno == EINTR);
        if (count <= 0)
        {
            throw std::runtime_error("JSONL read failed: " + snapshot.file.string());
        }
        cursor.pending.append(chunk.data(), static_cast<std::size_t>(count));
        cursor.offset += static_cast<std::uintmax_t>(count);
        EmitCompleteLines(snapshot.file, cursor, consume);
    }
    return true;
}

void JsonlReader::EmitCompleteLines(const std::filesystem::path &file, Cursor &cursor, const LineConsumer &consume)
{
    std::size_t begin = 0;
    std::size_t end;
    while ((end = cursor.pending.find('\n', begin)) != std::string::npos)
    {
        if (end - begin > options_.maxLineBytes)
        {
            throw std::runtime_error("JSONL line too large: " + file.string());
        }
        auto line = cursor.pending.substr(begin, end - begin);
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        consume({file, ++cursor.line, std::move(line)});
        begin = end + 1;
    }
    cursor.pending.erase(0, begin);
    if (cursor.pending.size() > options_.maxLineBytes)
    {
        throw std::runtime_error("JSONL tail too large: " + file.string());
    }
}

void JsonlReader::Finish(bool inputComplete, const LineConsumer &consume)
{
    for (auto &[file, cursor] : cursors_)
    {
        if (cursor.pending.empty())
        {
            continue;
        }
        if (inputComplete)
        {
            if (cursor.pending.back() == '\r')
            {
                cursor.pending.pop_back();
            }
            consume({file, ++cursor.line, std::move(cursor.pending)});
        }
        else
        {
            LOG(WARNING) << "Process stopped with partial JSONL line, rank: " << options_.rank << ", file: " << file
                         << ", line: " << cursor.line + 1 << ", bytes: " << cursor.pending.size();
        }
        cursor.pending.clear();
    }
}
}  // namespace msmonitor::process
