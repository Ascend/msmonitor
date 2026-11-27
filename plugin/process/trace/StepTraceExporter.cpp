// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#include "StepTraceExporter.h"

#include <archive.h>
#include <archive_entry.h>
#include <glog/logging.h>

#include <array>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <utility>

#include "TraceWriters.h"

namespace msmonitor::process
{
namespace
{
struct ArchiveConstants final
{
    static constexpr std::size_t ChunkBytes{256 * 1024};
};

void CheckArchive(archive *writer, int status)
{
    if (status != ARCHIVE_OK)
    {
        const auto *message = archive_error_string(writer);
        throw std::runtime_error(message ? message : "ZIP operation failed");
    }
}

void WriteZip(const std::filesystem::path &directory, const std::filesystem::path &file)
{
    using Writer = std::unique_ptr<archive, decltype(&archive_write_free)>;
    Writer writer(archive_write_new(), archive_write_free);
    if (!writer)
    {
        throw std::runtime_error("cannot allocate ZIP writer");
    }
    CheckArchive(writer.get(), archive_write_set_format_zip(writer.get()));
    CheckArchive(writer.get(), archive_write_set_options(writer.get(), "zip:compression=deflate"));
    CheckArchive(writer.get(), archive_write_open_filename(writer.get(), file.string().c_str()));
    std::array<char, ArchiveConstants::ChunkBytes> buffer{};
    for (const auto &item : std::filesystem::directory_iterator(directory))
    {
        if (item.is_symlink() || !item.is_regular_file())
        {
            throw std::runtime_error("unexpected entry in step directory");
        }
        std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(archive_entry_new(), archive_entry_free);
        if (!entry)
        {
            throw std::runtime_error("cannot allocate ZIP entry");
        }
        const auto name = directory.filename() / item.path().filename();
        archive_entry_set_pathname(entry.get(), name.generic_string().c_str());
        archive_entry_set_size(entry.get(), item.file_size());
        archive_entry_set_filetype(entry.get(), AE_IFREG);
        archive_entry_set_perm(entry.get(), 0600);
        CheckArchive(writer.get(), archive_write_header(writer.get(), entry.get()));
        std::ifstream input(item.path(), std::ios::binary);
        input.exceptions(std::ios::badbit);
        if (!input)
        {
            throw std::runtime_error("cannot read step file for compression");
        }
        while (input.read(buffer.data(), buffer.size()) || input.gcount() != 0)
        {
            const auto count = input.gcount();
            if (archive_write_data(writer.get(), buffer.data(), count) != count)
            {
                throw std::runtime_error("ZIP entry write failed");
            }
        }
        CheckArchive(writer.get(), archive_write_finish_entry(writer.get()));
    }
    CheckArchive(writer.get(), archive_write_close(writer.get()));
}
}  // namespace

StepTraceExporter::StepTraceExporter(TraceOptions options) : options_(std::move(options))
{
    if (options_.outputDir.empty() ||
        (options_.format != TraceFormat::ChromeTrace && options_.format != TraceFormat::MindStudioDb))
    {
        throw std::invalid_argument("invalid trace options");
    }
    options_.outputDir = std::filesystem::absolute(options_.outputDir).lexically_normal();
}

std::filesystem::path StepTraceExporter::WriteStep(const StepDataOutput &step)
{
    if (pending_ || step.rank < 0 || step.boundary.endNs <= step.boundary.startNs)
    {
        throw std::logic_error("invalid step or previous trace has not been compressed");
    }
    const auto parent = options_.outputDir / ("rank_" + std::to_string(step.rank));
    const auto directory = parent / ("step_" + std::to_string(step.boundary.stepId));
    const auto staging = parent / ("." + directory.filename().string() + ".tmp");
    std::filesystem::create_directories(parent);
    if (std::filesystem::exists(directory) || std::filesystem::exists(directory.string() + ".zip") ||
        !std::filesystem::create_directory(staging))
    {
        throw std::runtime_error("step trace output already exists: " + directory.string());
    }
    // Failures preserve the staging directory. Replays must explicitly choose a clean output root.
    if (options_.format == TraceFormat::ChromeTrace)
    {
        WriteChromeTrace(step, staging / "trace.json");
    }
    else
    {
        WriteMindStudioDb(step, staging / "trace.db");
    }
    std::filesystem::rename(staging, directory);
    pending_ = directory;
    return directory;
}

void StepTraceExporter::CompressStep(const std::filesystem::path &directory)
{
    if (!pending_ || *pending_ != directory)
    {
        throw std::logic_error("compression target is not the pending step");
    }
    const auto file = std::filesystem::path(directory.string() + ".zip");
    const auto staging = std::filesystem::path(file.string() + ".part");
    if (std::filesystem::exists(file) || std::filesystem::exists(staging))
    {
        throw std::runtime_error("step ZIP output already exists: " + file.string());
    }
    WriteZip(directory, staging);
    std::filesystem::rename(staging, file);
    if (!options_.keepDirectory)
    {
        std::filesystem::remove_all(directory);
    }
    pending_.reset();
    VLOG(1) << "Process trace compressed: " << file.string();
    if (options_.keepFiles != 0)
    {
        archives_.push_back(file);
        if (archives_.size() > options_.keepFiles)
        {
            std::filesystem::remove(archives_.front());
            archives_.pop_front();
        }
    }
}
}  // namespace msmonitor::process
