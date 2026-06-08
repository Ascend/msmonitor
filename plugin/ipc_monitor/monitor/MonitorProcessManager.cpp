/*
 * Copyright (C) 2026-2026. Huawei Technologies Co., Ltd. All rights reserved.
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

#include "monitor/MonitorProcessManager.h"

#include "monitor/Monitor.h"

namespace dynolog_npu
{
namespace ipc_monitor
{
namespace monitor
{
void MonitorProcessManager::RunPreTask()
{
    sessionStartTime_ = getCurrentTimestamp64();
    LOG(INFO) << "MonitorProcessManager start";
}

void MonitorProcessManager::RunPostTask()
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    sessionStartTime_ = 0;
    mstxMarkerHostData_.clear();
    mstxRangeHostData_.clear();
    mstxRangeDeviceData_.clear();
    LOG(INFO) << "MonitorProcessManager finish";
}

void MonitorProcessManager::ProcessApiData(msptiActivityApi *record, msptiActivityKind kind)
{
    uint64_t endTime = record->end;
    if (endTime < sessionStartTime_)
    {
        return;
    }
    std::string name = SafeCstrToString(record->name);
    Monitor::GetInstance()->ReportAPIData(
        {std::move(name), record->start, endTime, record->pt.processId, record->pt.threadId, record->correlationId},
        kind);
}

void MonitorProcessManager::ProcessCommunicationData(msptiActivityCommunication *record)
{
    uint64_t endTime = record->end;
    if (endTime < sessionStartTime_)
    {
        return;
    }
    std::string name = SafeCstrToString(record->name);
    std::string commName = SafeCstrToString(record->commName);
    std::string algType = SafeCstrToString(record->algType);
    Monitor::GetInstance()->ReportCommunicationData({std::move(name), record->start, endTime, record->ds.deviceId,
                                                     record->ds.streamId, record->count,
                                                     GetCommunicationDataTypeName(record->dataType),
                                                     std::move(commName), std::move(algType), record->correlationId});
}

void MonitorProcessManager::ProcessKernelData(msptiActivityKernel *record)
{
    uint64_t endTime = record->end;
    if (endTime < sessionStartTime_)
    {
        return;
    }
    std::string name = SafeCstrToString(record->name);
    std::string kernelType = SafeCstrToString(record->type);
    Monitor::GetInstance()->ReportKernelData({std::move(name), record->start, endTime, record->ds.deviceId,
                                              record->ds.streamId, record->correlationId, std::move(kernelType)});
}

void MonitorProcessManager::ProcessMstxData(msptiActivityMarker *record)
{
    if (record->timestamp < sessionStartTime_)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (record->sourceKind == msptiActivitySourceKind::MSPTI_ACTIVITY_SOURCE_KIND_HOST)
    {
        ProcessMstxHostData(record);
    }
    else if (record->sourceKind == msptiActivitySourceKind::MSPTI_ACTIVITY_SOURCE_KIND_DEVICE)
    {
        ProcessMstxDeviceData(record);
    }
}

void MonitorProcessManager::ProcessMstxHostData(msptiActivityMarker *record)
{
    uint64_t connectionId = record->id;
    uint64_t timestamp = record->timestamp;
    std::string message = SafeCstrToString(record->name);
    std::string domain = SafeCstrToString(record->domain);
    if (record->flag == msptiActivityFlag::MSPTI_ACTIVITY_FLAG_MARKER_INSTANTANEOUS ||
        record->flag == msptiActivityFlag::MSPTI_ACTIVITY_FLAG_MARKER_INSTANTANEOUS_WITH_DEVICE)
    {
        Monitor::GetInstance()->ReportMarkerData({message, "Host", domain, connectionId, timestamp, timestamp,
                                                  record->objectId.pt.processId, record->objectId.pt.threadId, 0, 0});
        if (record->flag == msptiActivityFlag::MSPTI_ACTIVITY_FLAG_MARKER_INSTANTANEOUS_WITH_DEVICE)
        {
            mstxMarkerHostData_.emplace(connectionId, MstxHostData{timestamp, domain, message});
        }
    }
    else if (record->flag == msptiActivityFlag::MSPTI_ACTIVITY_FLAG_MARKER_START ||
             record->flag == msptiActivityFlag::MSPTI_ACTIVITY_FLAG_MARKER_START_WITH_DEVICE)
    {
        mstxRangeHostData_.emplace(connectionId, MstxHostData{timestamp, domain, message});
    }
    else if (record->flag == msptiActivityFlag::MSPTI_ACTIVITY_FLAG_MARKER_END ||
             record->flag == msptiActivityFlag::MSPTI_ACTIVITY_FLAG_MARKER_END_WITH_DEVICE)
    {
        auto it = mstxRangeHostData_.find(connectionId);
        if (it != mstxRangeHostData_.end())
        {
            Monitor::GetInstance()->ReportMarkerData({it->second.message, "Host", it->second.domain, connectionId,
                                                      it->second.timestamp, timestamp, record->objectId.pt.processId,
                                                      record->objectId.pt.threadId, 0, 0});
            if (record->flag == msptiActivityFlag::MSPTI_ACTIVITY_FLAG_MARKER_END)
            {
                mstxRangeHostData_.erase(it);
            }
        }
    }
}

void MonitorProcessManager::ProcessMstxDeviceData(msptiActivityMarker *record)
{
    uint64_t connectionId = record->id;
    uint64_t timestamp = record->timestamp;
    uint32_t deviceId = static_cast<uint32_t>(record->objectId.ds.deviceId);
    if (record->flag == msptiActivityFlag::MSPTI_ACTIVITY_FLAG_MARKER_INSTANTANEOUS_WITH_DEVICE)
    {
        auto it = mstxMarkerHostData_.find(connectionId);
        Monitor::GetInstance()->ReportMarkerData(
            {it != mstxMarkerHostData_.end() ? it->second.message : SafeCstrToString(record->name), "Device",
             it != mstxMarkerHostData_.end() ? it->second.domain : SafeCstrToString(record->domain), connectionId,
             timestamp, timestamp, 0, 0, deviceId, record->objectId.ds.streamId});
        if (it != mstxMarkerHostData_.end())
        {
            mstxMarkerHostData_.erase(it);
        }
    }
    else if (record->flag == msptiActivityFlag::MSPTI_ACTIVITY_FLAG_MARKER_START_WITH_DEVICE)
    {
        mstxRangeDeviceData_.emplace(connectionId, MstxDeviceData{timestamp});
    }
    else if (record->flag == msptiActivityFlag::MSPTI_ACTIVITY_FLAG_MARKER_END_WITH_DEVICE)
    {
        auto it = mstxRangeDeviceData_.find(connectionId);
        if (it != mstxRangeDeviceData_.end())
        {
            auto hostIt = mstxRangeHostData_.find(connectionId);
            Monitor::GetInstance()->ReportMarkerData(
                {hostIt != mstxRangeHostData_.end() ? hostIt->second.message : SafeCstrToString(record->name), "Device",
                 hostIt != mstxRangeHostData_.end() ? hostIt->second.domain : SafeCstrToString(record->domain),
                 connectionId, it->second.timestamp, timestamp, 0, 0, deviceId, record->objectId.ds.streamId});
            mstxRangeDeviceData_.erase(it);
            if (hostIt != mstxRangeHostData_.end())
            {
                mstxRangeHostData_.erase(hostIt);
            }
        }
    }
}

ErrCode MonitorProcessManager::ConsumeMsptiData(msptiActivity *record)
{
    if (record == nullptr)
    {
        LOG(ERROR) << "MonitorProcessManager::ConsumeMsptiData record is null";
        return ErrCode::VALUE;
    }
    switch (record->kind)
    {
        case msptiActivityKind::MSPTI_ACTIVITY_KIND_API:
            ProcessApiData(ReinterpretConvert<msptiActivityApi *>(record), msptiActivityKind::MSPTI_ACTIVITY_KIND_API);
            break;
        case msptiActivityKind::MSPTI_ACTIVITY_KIND_COMMUNICATION:
            ProcessCommunicationData(ReinterpretConvert<msptiActivityCommunication *>(record));
            break;
        case msptiActivityKind::MSPTI_ACTIVITY_KIND_KERNEL:
            ProcessKernelData(ReinterpretConvert<msptiActivityKernel *>(record));
            break;
        case msptiActivityKind::MSPTI_ACTIVITY_KIND_MARKER:
            ProcessMstxData(ReinterpretConvert<msptiActivityMarker *>(record));
            break;
        case msptiActivityKind::MSPTI_ACTIVITY_KIND_RUNTIME_API:
            ProcessApiData(ReinterpretConvert<msptiActivityApi *>(record),
                           msptiActivityKind::MSPTI_ACTIVITY_KIND_RUNTIME_API);
            break;
        case msptiActivityKind::MSPTI_ACTIVITY_KIND_ACL_API:
            ProcessApiData(ReinterpretConvert<msptiActivityApi *>(record),
                           msptiActivityKind::MSPTI_ACTIVITY_KIND_ACL_API);
            break;
        case msptiActivityKind::MSPTI_ACTIVITY_KIND_NODE_API:
            ProcessApiData(ReinterpretConvert<msptiActivityApi *>(record),
                           msptiActivityKind::MSPTI_ACTIVITY_KIND_NODE_API);
            break;
        default:
            LOG(WARNING) << record->kind << " is not supported for MonitorProcessManager";
            break;
    }
    return ErrCode::SUC;
}
}  // namespace monitor
}  // namespace ipc_monitor
}  // namespace dynolog_npu
