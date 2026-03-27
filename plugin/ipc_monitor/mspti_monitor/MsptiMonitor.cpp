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
#include "MsptiMonitor.h"

#include <algorithm>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <glog/logging.h>

#include "DynoLogNpuMonitor.h"
#include "MetricManager.h"
#include "db/DBProcessManager.h"
#include "jsonl/JsonlProcessManager.h"
#include "utils.h"

namespace {
constexpr size_t DEFAULT_BUFFER_SIZE = 8 * 1024 * 1024;
constexpr size_t MAX_BUFFER_SIZE = 256 * 1024 * 1024;
constexpr uint32_t MAX_ALLOC_CNT = MAX_BUFFER_SIZE / DEFAULT_BUFFER_SIZE;
const std::unordered_set<msptiActivityKind> FILTER_WHITE_LIST = {
    MSPTI_ACTIVITY_KIND_MEMORY,
    MSPTI_ACTIVITY_KIND_MEMSET,
    MSPTI_ACTIVITY_KIND_MEMCPY
};
const std::unordered_set<msptiActivityFlag> FLAGS_WITH_VALID_NAME = {
    MSPTI_ACTIVITY_FLAG_MARKER_INSTANTANEOUS,
    MSPTI_ACTIVITY_FLAG_MARKER_INSTANTANEOUS_WITH_DEVICE,
    MSPTI_ACTIVITY_FLAG_MARKER_START,
    MSPTI_ACTIVITY_FLAG_MARKER_START_WITH_DEVICE
};
}

namespace dynolog_npu {
namespace ipc_monitor {
MsptiMonitor::~MsptiMonitor()
{
    Uninit();
}

void MsptiMonitor::Start()
{
    if (start_.load()) {
        return;
    }
    if (dataProcessor_ == nullptr) {
        if (savePath_.empty()) {
            std::shared_ptr<metric::MetricManager> metricManager{nullptr};
            MakeSharedPtr(metricManager);
            dataProcessor_ = metricManager;
        } else {
            if (exportType_ == MSPTI_EXPORT_TYPE_DB) {
                std::shared_ptr<db::DBProcessManager> dbProcessManager{nullptr};
                MakeSharedPtr(dbProcessManager, savePath_);
                dataProcessor_ = dbProcessManager;
            } else if (exportType_ == MSPTI_EXPORT_TYPE_JSONL) {
                std::shared_ptr<jsonl::JsonlProcessManager> jsonlProcessManager{nullptr};
                MakeSharedPtr(jsonlProcessManager, savePath_);
                dataProcessor_ = jsonlProcessManager;
            } else {
                LOG(ERROR) << "DataProcessor init failed, export_type: " << exportType_ << " is invalid.";
            }
        }
    }
    if (dataProcessor_ == nullptr) {
        LOG(ERROR) << "MsptiMonitor Start failed, dataProcessor init failed";
        return;
    }

    // subscribe and register callbacks to mspti first, ensure before enable the activity
    if (msptiSubscribe(&subscriber_, nullptr, nullptr) != MSPTI_SUCCESS) {
        LOG(ERROR) << "MsptiMonitor start failed, msptiSubscribe failed";
        return;
    }
    if (msptiActivityRegisterCallbacks(BufferRequest, BufferComplete) != MSPTI_SUCCESS) {
        LOG(ERROR) << "MsptiMonitor start failed, msptiActivityRegisterCallbacks failed";
        return;
    }

    SetThreadName("MsptiMonitor");
    if (Thread::Start() != 0) {
        LOG(ERROR) << "MsptiMonitor start failed";
        return;
    }
    start_.store(true);
    dataProcessor_->SetReportInterval(flushInterval_);
    dataProcessor_->Run();
    LOG(INFO) << "MsptiMonitor start successfully";
}

void MsptiMonitor::Stop()
{
    if (!start_.load()) {
        LOG(WARNING) << "MsptiMonitor is not running";
        return;
    }

    if (msptiActivityFlushAll(1) != MSPTI_SUCCESS) {
        LOG(WARNING) << "MsptiMonitor stop msptiActivityFlushAll failed";
    }
    Uninit();
    LOG(INFO) << "MsptiMonitor stop successfully";
}

void MsptiMonitor::Uninit()
{
    if (!start_.load()) {
        return;
    }
    start_.store(false);
    Thread::Stop();
    if (dataProcessor_ != nullptr) {
        dataProcessor_->Stop();
        dataProcessor_ = nullptr;
    }
    savePath_.clear();
    exportType_.clear();
    {
        std::lock_guard<std::mutex> lock(filterMtx_);
        filterItems_.clear();
    }
}

bool MsptiMonitor::CheckAndSetSavePath(const std::string &path)
{
    if (path.empty()) {
        LOG(ERROR) << "MsptiMonitor CheckAndSetSavePath failed, path is empty";
        return false;
    }
    std::string absPath = PathUtils::RelativeToAbsPath(path);
    if (PathUtils::DirPathCheck(absPath)) {
        std::string realPath = PathUtils::RealPath(absPath);
        if (PathUtils::CreateDir(realPath)) {
            savePath_ = realPath;
            return true;
        }
        LOG(ERROR) << "MsptiMonitor CheckAndSetSavePath failed, Create save path: " << realPath << " failed.";
    } else {
        LOG(ERROR) << "MsptiMonitor CheckAndSetSavePath failed, save path: " << absPath << " is invalid.";
    }
    return false;
}

void MsptiMonitor::EnableActivity(msptiActivityKind kind)
{
    if (MSPTI_ACTIVITY_KIND_INVALID < kind && kind < MSPTI_ACTIVITY_KIND_COUNT) {
        std::lock_guard<std::mutex> lock(activityMtx_);
        if (msptiActivityEnable(kind) == MSPTI_SUCCESS) {
            enabledActivities_.insert(kind);
        } else {
            LOG(ERROR) << "MsptiMonitor enableActivity failed, kind: " << static_cast<int32_t>(kind);
        }
        if (dataProcessor_ != nullptr) {
            dataProcessor_->EnableKindSwitch(kind, true);
        }
    }
}

void MsptiMonitor::DisableActivity(msptiActivityKind kind)
{
    if (MSPTI_ACTIVITY_KIND_INVALID < kind && kind < MSPTI_ACTIVITY_KIND_COUNT) {
        std::lock_guard<std::mutex> lock(activityMtx_);
        if (msptiActivityDisable(kind) == MSPTI_SUCCESS) {
            enabledActivities_.erase(kind);
        } else {
            LOG(ERROR) << "MsptiMonitor disableActivity failed, kind: " << static_cast<int32_t>(kind);
        }
        if (dataProcessor_ != nullptr) {
            dataProcessor_->EnableKindSwitch(kind, false);
        }
    }
}

void MsptiMonitor::SetFlushInterval(uint32_t interval)
{
    flushInterval_.store(interval);
    if (dataProcessor_ != nullptr) {
        dataProcessor_->SetReportInterval(interval);
    }
}

void MsptiMonitor::SetDuration(float duration)
{
    duration_.store(duration);
}

bool MsptiMonitor::IsStarted()
{
    return start_.load();
}

std::set<msptiActivityKind> MsptiMonitor::GetEnabledActivities()
{
    std::lock_guard<std::mutex> lock(activityMtx_);
    return enabledActivities_;
}

void MsptiMonitor::SetFilterItems(const msptiFilterItems& filterItems)
{
    std::lock_guard<std::mutex> lock(filterMtx_);
    filterItems_ = filterItems;
}

bool MsptiMonitor::ShouldKeepRecord(msptiActivity *record)
{
    if (record == nullptr) {
        LOG(ERROR) << "MsptiData record is null";
        return false;
    }
    msptiFilterItems currFilterItems;
    {
        std::lock_guard<std::mutex> lock(filterMtx_);
        currFilterItems = filterItems_;
    }
    if (currFilterItems.empty() || FILTER_WHITE_LIST.find(record->kind) != FILTER_WHITE_LIST.end()) {
        return true;
    }
    auto it = currFilterItems.find(record->kind);
    if (it == currFilterItems.end()) {
        return false;
    }
    std::string opName;
    switch (record->kind) {
        case msptiActivityKind::MSPTI_ACTIVITY_KIND_API:
        case msptiActivityKind::MSPTI_ACTIVITY_KIND_ACL_API:
        case msptiActivityKind::MSPTI_ACTIVITY_KIND_NODE_API:
        case msptiActivityKind::MSPTI_ACTIVITY_KIND_RUNTIME_API: {
            auto* data = ReinterpretConvert<msptiActivityApi*>(record);
            opName = (data != nullptr) ? data->name : "";
            break;
        }
        case msptiActivityKind::MSPTI_ACTIVITY_KIND_COMMUNICATION: {
            auto* data = ReinterpretConvert<msptiActivityCommunication*>(record);
            opName = (data != nullptr) ? data->name : "";
            break;
        }
        case msptiActivityKind::MSPTI_ACTIVITY_KIND_KERNEL: {
            auto* data = ReinterpretConvert<msptiActivityKernel*>(record);
            opName = (data != nullptr) ? data->name : "";
            break;
        }
        case msptiActivityKind::MSPTI_ACTIVITY_KIND_MARKER: {
            auto* data = ReinterpretConvert<msptiActivityMarker*>(record);
            if (data == nullptr) {
                return false;
            }
            if (data->sourceKind == msptiActivitySourceKind::MSPTI_ACTIVITY_SOURCE_KIND_HOST &&
                FLAGS_WITH_VALID_NAME.find(data->flag) != FLAGS_WITH_VALID_NAME.end()) {
                opName = data->name;
                break;
            } else {
                return true;
            }
        }
        default:
            return false;
    }
    if (opName.empty()) {
        return false;
    }
    return std::any_of(it->second.begin(), it->second.end(),
        [&opName](const auto& filterOp) {
            return opName.find(filterOp) != std::string::npos;
        }
    );
}

void MsptiMonitor::Run()
{
    auto startTime = std::chrono::steady_clock::now();
    auto lastFlushTime = startTime;
    auto isDurationExpired = false;
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        if (duration_.load() > 0) {
            auto currentTime = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count();
            if (elapsed >= static_cast<long long>(duration_.load() * 1000)) {
                isDurationExpired = true;
                LOG(INFO) << "MsptiMonitor run duration: " << duration_.load() << "s expired";
                break;
            }
        }

        if (flushInterval_.load() > 0) {
            auto currentTime = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastFlushTime).count();
            if (elapsed >= static_cast<long long>(flushInterval_.load() * 1000)) {
                lastFlushTime = currentTime;
                if (msptiActivityFlushAll(1) != MSPTI_SUCCESS) {
                    LOG(ERROR) << "MsptiMonitor run msptiActivityFlushAll failed";
                }
            }
        }

        if (!start_.load()) {
            break;
        }
    }
    if (msptiUnsubscribe(subscriber_) != MSPTI_SUCCESS) {
        LOG(ERROR) << "MsptiMonitor run failed, msptiUnsubscribe failed";
    }
    {
        std::lock_guard<std::mutex> lock(activityMtx_);
        for (auto kind : enabledActivities_) {
            msptiActivityDisable(kind);
        }
        enabledActivities_.clear();
    }
    flushInterval_.store(0);
    if (isDurationExpired) {
        if (msptiActivityFlushAll(1) != MSPTI_SUCCESS) {
            LOG(WARNING) << "MsptiMonitor stop msptiActivityFlushAll failed";
        }
        start_.store(false);
        if (dataProcessor_ != nullptr) {
            dataProcessor_->Stop();
            dataProcessor_ = nullptr;
        }
        savePath_.clear();
        exportType_.clear();
        {
            std::lock_guard<std::mutex> lock(filterMtx_);
            filterItems_.clear();
        }
        LOG(INFO) << "MsptiMonitor stop successfully";
    }
}

std::atomic<uint32_t> MsptiMonitor::allocCnt{0};

void MsptiMonitor::BufferRequest(uint8_t **buffer, size_t *size, size_t *maxNumRecords)
{
    if (buffer == nullptr || size == nullptr || maxNumRecords == nullptr) {
        return;
    }
    *maxNumRecords = 0;
    if (allocCnt.load() >= MAX_ALLOC_CNT) {
        *buffer = nullptr;
        *size = 0;
        LOG(ERROR) << "MsptiMonitor BufferRequest failed, allocCnt: " << allocCnt.load();
        return;
    }
    uint8_t *pBuffer = ReinterpretConvert<uint8_t*>(MsptiMalloc(DEFAULT_BUFFER_SIZE, ALIGN_SIZE));
    if (pBuffer == nullptr) {
        *buffer = nullptr;
        *size = 0;
    } else {
        *buffer = pBuffer;
        *size = DEFAULT_BUFFER_SIZE;
        allocCnt++;
        LOG(INFO) << "MsptiMonitor BufferRequest, size: " << *size;
    }
}

void MsptiMonitor::BufferComplete(uint8_t *buffer, size_t size, size_t validSize)
{
    if (validSize > 0 && buffer != nullptr) {
        LOG(INFO) << "MsptiMonitor BufferComplete, size: " << size << ", validSize: " << validSize;
        msptiActivity *record = nullptr;
        msptiResult status = MSPTI_SUCCESS;
        do {
            status = msptiActivityGetNextRecord(buffer, validSize, &record);
            if (status == MSPTI_SUCCESS) {
                if (GetInstance()->ShouldKeepRecord(record)) {
                    BufferConsume(record);
                }
            } else if (status == MSPTI_ERROR_MAX_LIMIT_REACHED) {
                break;
            } else {
                LOG(ERROR) << "MsptiMonitor BufferComplete failed, status: " << static_cast<int32_t>(status);
                break;
            }
        } while (true);
        allocCnt--;
    }
    MsptiFree(buffer);
}

void MsptiMonitor::BufferConsume(msptiActivity *record)
{
    if (record == nullptr) {
        return;
    }
    auto dataProcessor = GetDataProcessor();
    if (dataProcessor != nullptr) {
        dataProcessor->ConsumeMsptiData(record);
    }
}

std::shared_ptr<MsptiDataProcessBase> MsptiMonitor::GetDataProcessor()
{
    return GetInstance()->dataProcessor_;
}
} // namespace ipc_monitor
} // namespace dynolog_npu
