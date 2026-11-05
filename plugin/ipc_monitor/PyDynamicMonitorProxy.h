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
#ifndef PYDYNAMIC_MONITOR_PROXY_H
#define PYDYNAMIC_MONITOR_PROXY_H

#include <glog/logging.h>

#include <cstdio>

#include "DynoLogNpuMonitor.h"
#include "MonitorBase.h"
#include "securec.h"
#include "utils.h"

namespace dynolog_npu
{
namespace ipc_monitor
{

enum PROFILER_STATUS : int32_t
{
    UNINITIALIZED = -1,
    IDLE = 0,
    RUNNING = 1,
    READY = 2,
};

int32_t GetInt32FromMap(const std::unordered_map<std::string, std::string>& map, const std::string& key,
                        int32_t default_val = -1)
{
    auto it = map.find(key);
    if (it != map.end())
    {
        int32_t val;
        if (!Str2Int32(val, it->second))
        {
            return default_val;
        }
        return val;
    }
    return default_val;
}

std::string GetStrFromMap(const std::unordered_map<std::string, std::string>& map, const std::string& key,
                          const std::string& default_val = "")
{
    auto it = map.find(key);
    if (it != map.end())
    {
        return it->second;
    }
    return default_val;
}

class PyDynamicMonitorProxy : public Singleton<PyDynamicMonitorProxy>
{
    friend class Singleton<PyDynamicMonitorProxy>;

   public:
    PyDynamicMonitorProxy() = default;
    bool InitDyno(int npuId)
    {
        try
        {
            monitor_ = DynoLogNpuMonitor::GetInstance();
            monitor_->SetNpuId(npuId);
            bool res = monitor_->Init();
            if (res)
            {
                NpuStatus npuStatus;
                npuStatus.status = PROFILER_STATUS::IDLE;
                DynoLogNpuMonitor::GetInstance()->UpdateNpuStatus(npuStatus, MSG_TYPE_TRACE_STATUS);
                DynoLogNpuMonitor::GetInstance()->UpdateNpuStatus(npuStatus, MSG_TYPE_MONITOR_STATUS);
            }
            return res;
        }
        catch (const std::exception& e)
        {
            LOG(ERROR) << "Error when init dyno " << e.what();
            return false;
        }
    }

    std::string PollDyno() { return monitor_->Poll(); }

    void EnableMsptiMonitor(std::unordered_map<std::string, std::string>& config_map)
    {
        DynoLogNpuMonitor::GetInstance()->EnableMsptiMonitor(config_map);
    }

    void FinalizeDyno() const { DynoLogNpuMonitor::GetInstance()->Finalize(); }

    void UpdateProfilerStatus(std::unordered_map<std::string, std::string>& status)
    {
        NpuStatus npuStatus;
        npuStatus.status = GetInt32FromMap(status, PROFILER_STATUS);
        npuStatus.currentStep = GetInt32FromMap(status, CURRENT_STEP);
        npuStatus.startStep = GetInt32FromMap(status, START_STEP);
        npuStatus.stopStep = GetInt32FromMap(status, STOP_STEP);
        const std::string traceId = GetStrFromMap(status, TRACE_ID);
        if (!traceId.empty())
        {
            constexpr size_t kTraceIdCapacity = sizeof(npuStatus.traceId);
            const size_t traceIdLen = traceId.size();
            const size_t copyLen = traceIdLen < kTraceIdCapacity ? traceIdLen : kTraceIdCapacity - 1;
            if (traceIdLen >= kTraceIdCapacity)
            {
                LOG(WARNING) << "trace id length " << traceIdLen << " exceeds buffer capacity " << kTraceIdCapacity
                             << ", truncated to " << copyLen << " bytes";
            }
            if (memcpy_s(npuStatus.traceId, kTraceIdCapacity, traceId.c_str(), copyLen) != EOK)
            {
                LOG(ERROR) << "Failed to copy trace id " << IPC_ERROR(ErrCode::MEMORY);
            }
            else
            {
                npuStatus.traceId[copyLen] = '\0';
            }
        }
        npuStatus.queueDepth = GetInt32FromMap(status, QUEUE_DEPTH, 0);
        npuStatus.rejected = GetInt32FromMap(status, REJECTED, 0);
        // Legacy reporters omit these keys, so keep extended=0 and let status hide them.
        const bool hasExtended = status.count(TRACE_ID) && status.count(QUEUE_DEPTH) && status.count(REJECTED);
        npuStatus.extended = hasExtended ? 1 : 0;
        DynoLogNpuMonitor::GetInstance()->UpdateNpuStatus(npuStatus, MSG_TYPE_TRACE_STATUS);
        if (npuStatus.status == PROFILER_STATUS::UNINITIALIZED)
        {
            DynoLogNpuMonitor::GetInstance()->UpdateNpuStatus(npuStatus, MSG_TYPE_MONITOR_STATUS);
        }
    }

   private:
    MonitorBase* monitor_ = nullptr;
};

}  // namespace ipc_monitor
}  // namespace dynolog_npu
#endif  // PYDYNAMIC_MONITOR_PROXY_H
