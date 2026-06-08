/*
 * Copyright (C) 2025-2025. Huawei Technologies Co., Ltd. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "jsonl/RotateLogger.h"

#include <glog/logging.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include "utils.h"

namespace fs = std::filesystem;

namespace dynolog_npu
{
namespace ipc_monitor
{
namespace jsonl
{
namespace
{
inline uint64_t GetCurrentMilliseconds()
{
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

std::string FormatTimeStampMs(uint64_t ms)
{
    auto time = std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
    std::time_t timeT = std::chrono::system_clock::to_time_t(time);
    std::tm tm{};
    std::stringstream ss;
    if (localtime_r(&timeT, &tm) != nullptr)
    {
        ss << std::put_time(&tm, "%Y%m%d%H%M%S");
        constexpr int kMilliTimeWidth = 3;
        constexpr int kMilliTimeDivisor = 1000;
        ss << std::setw(kMilliTimeWidth) << std::setfill('0') << (ms % kMilliTimeDivisor);
    }
    return ss.str();
}

std::string GetMsmonitorJsonlName(uint64_t ms, const std::string &outputPath)
{
    auto identity = join({std::to_string(GetProcessId()), FormatTimeStampMs(ms), std::to_string(GetRankId())}, "_");
    return outputPath + "/msmonitor_" + identity + ".jsonl";
}
}  // namespace

RotateLogger::~RotateLogger() { UnInit(); }

void RotateLogger::UnInit()
{
    if (!initialized_)
    {
        LOG(WARNING) << "RotateLogger not initialized";
        return;
    }
    initialized_ = false;
    if (logFile_.is_open())
    {
        logFile_.close();
        logFile_.clear();
    }
    logFiles_.clear();
    lastLogFileTime_ = 0;
    curLines_ = 0;
}

void RotateLogger::Log(std::string message)
{
    if (!initialized_)
    {
        LOG(WARNING) << "RotateLogger not initialized";
        return;
    }
    if (message.empty())
    {
        LOG(WARNING) << "Empty message";
        return;
    }
    if (curLines_ >= maxLines_)
    {
        Rotate();
    }
    if (!logFile_.is_open() && !OpenNewFile())
    {
        LOG(ERROR) << "RotateLogger open new log file failed";
        return;
    }
    logFile_.write(message.c_str(), message.size());
    ++curLines_;
}

void RotateLogger::Flush()
{
    if (!initialized_)
    {
        LOG(WARNING) << "RotateLogger not initialized";
        return;
    }
    if (logFile_.is_open())
    {
        logFile_.flush();
    }
}

bool RotateLogger::OpenNewFile()
{
    if (logFile_.is_open())
    {
        logFile_.close();
        logFile_.clear();
    }
    auto curTime = GetCurrentMilliseconds();
    if (lastLogFileTime_ >= curTime)
    {
        curTime = lastLogFileTime_ + 1;
    }
    lastLogFileTime_ = curTime;
    auto fileName = GetMsmonitorJsonlName(lastLogFileTime_, logDir_);
    if (!PathUtils::CreateDir(logDir_))
    {
        LOG(ERROR) << "RotateLogger create log dir failed, path: " << logDir_;
        return false;
    }
    logFile_.open(fileName, std::ios::out | std::ios::trunc);
    if (!logFile_.is_open())
    {
        LOG(ERROR) << "RotateLogger create log file failed, path: " << fileName;
        return false;
    }
    LOG(INFO) << "RotateLogger create log file, path: " << fileName;
    curLines_ = 0;
    logFiles_.emplace_back(std::move(fileName));
    return true;
}

void RotateLogger::Rotate()
{
    if (logFile_.is_open())
    {
        logFile_.close();
        logFile_.clear();
    }
    if (maxFiles_ > 0)
    {
        ManageFiles();
    }
    OpenNewFile();
}

void RotateLogger::ManageFiles()
{
    if (logFiles_.size() < maxFiles_)
    {
        return;
    }
    auto end = std::remove_if(logFiles_.begin(), logFiles_.end(), [](const std::string &file)
                              { return !PathUtils::IsFileExist(file) || !PathUtils::IsOwner(file); });
    logFiles_.erase(end, logFiles_.end());
    std::sort(logFiles_.begin(), logFiles_.end(), [](const std::string &a, const std::string &b)
              { return fs::last_write_time(a) < fs::last_write_time(b); });
    int filesToRemove = logFiles_.size() - maxFiles_ + 1;
    for (auto it = logFiles_.begin(); it != logFiles_.begin() + filesToRemove; ++it)
    {
        std::error_code ec;
        if (!fs::remove(*it, ec))
        {
            LOG(ERROR) << "RotateLogger remove log file failed, path: " << *it << ", error: " << ec.message();
        }
        else
        {
            LOG(INFO) << "RotateLogger remove log file, path: " << *it;
        }
    }
    logFiles_.erase(logFiles_.begin(), logFiles_.begin() + filesToRemove);
}
}  // namespace jsonl
}  // namespace ipc_monitor
}  // namespace dynolog_npu
