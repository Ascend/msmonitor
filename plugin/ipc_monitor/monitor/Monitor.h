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

#ifndef MSMONITOR_MONITOR_H
#define MSMONITOR_MONITOR_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_set>
#include <vector>

#include "dcmi/DcmiRingBuffer.h"
#include "dcmi/DcmiTypes.h"
#include "monitor/ActivityData.h"
#include "mspti.h"
#include "singleton.h"

namespace dynolog_npu
{
namespace ipc_monitor
{
namespace dcmi
{
class DcmiCollector;
}
namespace monitor
{

// DCMI 环形缓冲默认容量：1M 条样本，满时覆盖最旧（内部固定策略）。
constexpr size_t DCMI_RING_CAPACITY = 1000000;

class Monitor : public Singleton<Monitor>
{
   public:
    Monitor();
    virtual ~Monitor();

    // 统一配置入口：kinds（mspti 活动类型）+ dcmiLayers（硬件层）。dcmiMetrics 保留内部扩展用。
    // 新采集会话启动时自动 Clear 全部历史数据（mspti 与 dcmi），避免上次会话残留（P2 修复）。
    void Start(const std::vector<msptiActivityKind>& kinds, const std::set<DcmiLayer>& dcmiLayers = {},
               const std::set<DcmiMetricKind>& dcmiMetrics = {}, uint32_t dcmiIntervalMs = 10,
               const std::vector<uint32_t>& devices = {});
    void Stop();
    std::unordered_set<msptiActivityKind> GetKinds() const { return kinds_; };
    std::vector<API> GetAPIData() const { return apiData_; };
    std::vector<API> GetAclApiData() const { return aclApiData_; };
    std::vector<API> GetNodeApiData() const { return nodeApiData_; };
    std::vector<API> GetRuntimeApiData() const { return runtimeApiData_; };
    std::vector<Kernel> GetKernelData() const { return kernelData_; };
    std::vector<Communication> GetCommunicationData() const { return communicationData_; };
    std::vector<Marker> GetMarkerData() const { return markerData_; };
    std::vector<DcmiSample> GetDcmiData() const;
    // DFX：加载/符号/指标使能/采集计数快照。
    DcmiStatusSnapshot GetDcmiStatus() const;
    std::vector<DcmiMetricMeta> GetDcmiMetricMeta() const;
    void ClearDcmiDataForTest();

    void ReportAPIData(API&& api, msptiActivityKind kind);
    void ReportKernelData(Kernel&& kernel);
    void ReportCommunicationData(Communication&& communication);
    void ReportMarkerData(Marker&& marker);
    void ReportDcmiData(DcmiSample&& sample);

   private:
    void Clear();
    void ClearDcmiData();
    bool StartDcmi(const std::set<DcmiLayer>& layers, const std::set<DcmiMetricKind>& metrics, uint32_t intervalMs,
                   const std::vector<uint32_t>& devices);
    void StopDcmi();

   private:
    std::unordered_set<msptiActivityKind> kinds_;
    std::mutex apiMutex_;
    std::vector<API> apiData_;
    std::vector<API> aclApiData_;
    std::vector<API> nodeApiData_;
    std::vector<API> runtimeApiData_;
    std::mutex kernelMutex_;
    std::vector<Kernel> kernelData_;
    std::mutex communicationMutex_;
    std::vector<Communication> communicationData_;
    std::mutex markerMutex_;
    std::vector<Marker> markerData_;

    mutable std::mutex dcmiMutex_;
    DcmiRingBuffer dcmiRing_;
    uint64_t dcmiDropCount_ = 0;  // ring 满覆盖次数（DFX）
    std::unique_ptr<dcmi::DcmiCollector> dcmiCollector_;
};
}  // namespace monitor
}  // namespace ipc_monitor
}  // namespace dynolog_npu

#endif  // MSMONITOR_MONITOR_H
