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
#ifndef MSPTI_MONITOR_H
#define MSPTI_MONITOR_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <set>

#include "InputParser.h"
#include "MsptiDataProcessBase.h"
#include "mspti.h"
#include "singleton.h"
#include "thread.h"

namespace dynolog_npu
{
namespace ipc_monitor
{
const std::string MSPTI_EXPORT_TYPE_DB = "DB";
const std::string MSPTI_EXPORT_TYPE_JSONL = "Jsonl";

class MsptiMonitor : public Singleton<MsptiMonitor>, public Thread
{
   public:
    virtual ~MsptiMonitor();
    void Start();
    void Start(const MsptiMonitorCfg& cmd);
    void Stop();
    void EnableActivity(msptiActivityKind kind);
    void DisableActivity(msptiActivityKind kind);
    void SetFlushInterval(uint32_t interval);
    void SetDuration(float duration);
    bool IsStarted();
    std::set<msptiActivityKind> GetEnabledActivities();
    void Uninit();
    bool CheckAndSetSavePath(const std::string& path);
    bool IsMetricMode() const { return savePath_.empty(); }
    void SetExportType(const std::string& type) { exportType_ = type; }
    void SetDataProcessor(std::shared_ptr<MsptiDataProcessBase> dataProcessor) { dataProcessor_ = dataProcessor; }
    void SetClusterConfigData(const std::unordered_map<std::string, std::string>& configData)
    {
        clusterConfigData_ = configData;
    }
    void SetFilterItems(const msptiFilterItems& filterItems);
    bool ShouldKeepRecord(msptiActivity* record);
    const std::unordered_map<std::string, std::string>& GetClusterConfigData() const { return clusterConfigData_; }

   private:
    static void BufferRequest(uint8_t** buffer, size_t* size, size_t* maxNumRecords);
    static void BufferComplete(uint8_t* buffer, size_t size, size_t validSize);
    static void BufferConsume(msptiActivity* record);
    static std::shared_ptr<MsptiDataProcessBase> GetDataProcessor();
    static std::atomic<uint32_t> allocCnt;

   private:
    void Run() override;

   private:
    std::atomic<bool> start_{false};
    msptiSubscriberHandle subscriber_{nullptr};
    std::mutex activityMtx_;
    std::set<msptiActivityKind> enabledActivities_;
    std::atomic<uint32_t> flushInterval_{0};
    std::atomic<float> duration_{0.0f};
    std::string savePath_;
    std::string exportType_;
    std::mutex filterMtx_;
    msptiFilterItems filterItems_;
    std::shared_ptr<MsptiDataProcessBase> dataProcessor_{nullptr};
    std::unordered_map<std::string, std::string> clusterConfigData_;
};
}  // namespace ipc_monitor
}  // namespace dynolog_npu
#endif  // MSPTI_MONITOR_H
