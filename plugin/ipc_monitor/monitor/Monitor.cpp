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

#include "monitor/Monitor.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include "MsptiMonitor.h"
#include "dcmi/DcmiCollector.h"
#include "dcmi/DcmiMetricRegistry.h"
#include "monitor/MonitorProcessManager.h"

namespace dynolog_npu
{
namespace ipc_monitor
{
namespace monitor
{

constexpr uint32_t DEFAULT_CAPACITY = 1024;

Monitor::Monitor()
{
    // init log
    InitMsMonitorLog();
    dcmiRing_.Init(DCMI_RING_CAPACITY);
}

Monitor::~Monitor() { StopDcmi(); }

void Monitor::Start(const std::vector<msptiActivityKind> &kinds, const std::set<DcmiLayer> &dcmiLayers,
                    const std::set<DcmiMetricKind> &dcmiMetrics, uint32_t dcmiIntervalMs,
                    const std::vector<uint32_t> &devices)
{
    const std::unordered_set<msptiActivityKind> kSupportedKinds = {
        msptiActivityKind::MSPTI_ACTIVITY_KIND_API,        msptiActivityKind::MSPTI_ACTIVITY_KIND_KERNEL,
        msptiActivityKind::MSPTI_ACTIVITY_KIND_MARKER,     msptiActivityKind::MSPTI_ACTIVITY_KIND_COMMUNICATION,
        msptiActivityKind::MSPTI_ACTIVITY_KIND_ACL_API,    msptiActivityKind::MSPTI_ACTIVITY_KIND_NODE_API,
        msptiActivityKind::MSPTI_ACTIVITY_KIND_RUNTIME_API};

    auto validKinds = kinds;
    validKinds.erase(std::remove_if(validKinds.begin(), validKinds.end(), [&kSupportedKinds](msptiActivityKind kind)
                                    { return kSupportedKinds.find(kind) == kSupportedKinds.end(); }),
                     validKinds.end());

    if (validKinds.empty() && dcmiLayers.empty() && dcmiMetrics.empty())
    {
        LOG(WARNING) << "Invalid MsptiActivityKind and no dcmi layer/metric";
        return;
    }

    bool needMspti = !validKinds.empty();
    auto msptiMonitor = MsptiMonitor::GetInstance();
    if (needMspti && msptiMonitor->IsStarted())
    {
        LOG(WARNING) << "MsptiMonitor already started";
        return;
    }

    // 会话语义：新会话（MsptiMonitor 未运行）启动前一律清空全部历史数据，
    // 无论本次是否包含 mspti kinds —— 避免 DCMI-only 会话读到上次残留（P2 修复）。
    if (!msptiMonitor->IsStarted())
    {
        Clear();
    }

    if (needMspti)
    {
        std::shared_ptr<MonitorProcessManager> processManager{nullptr};
        MakeSharedPtr(processManager);
        msptiMonitor->SetDataProcessor(processManager);
        msptiMonitor->Start();
        kinds_ = std::unordered_set<msptiActivityKind>(validKinds.begin(), validKinds.end());
        for (auto kind : kinds_)
        {
            msptiMonitor->EnableActivity(kind);
        }
    }

    if (!dcmiLayers.empty() || !dcmiMetrics.empty())
    {
        StartDcmi(dcmiLayers, dcmiMetrics, dcmiIntervalMs, devices);
    }

    LOG(INFO) << "monitor started, mspti=" << (needMspti ? 1 : 0)
              << " dcmi=" << ((!dcmiLayers.empty() || !dcmiMetrics.empty()) ? 1 : 0);
}

bool Monitor::StartDcmi(const std::set<DcmiLayer> &layers, const std::set<DcmiMetricKind> &metrics, uint32_t intervalMs,
                        const std::vector<uint32_t> &devices)
{
    StopDcmi();
    ClearDcmiData();

    // 层 → kind 展开（Python 按硬件层配置的主入口）
    auto *registry = dcmi::DcmiMetricRegistry::GetInstance();
    std::set<DcmiMetricKind> enabled = metrics;
    for (auto layer : layers)
    {
        auto kinds = registry->KindsForLayer(layer);
        enabled.insert(kinds.begin(), kinds.end());
    }
    if (enabled.empty())
    {
        LOG(WARNING) << "StartDcmi: no enabled metric";
        return false;
    }

    // ring 容量为内部固定策略（默认 100 万，覆盖最旧），不对外暴露
    {
        std::lock_guard<std::mutex> lock(dcmiMutex_);
        dcmiRing_.Init(DCMI_RING_CAPACITY);
        dcmiDropCount_ = 0;
    }

    auto collector = std::make_unique<dcmi::DcmiCollector>();
    if (!collector->Start(enabled, intervalMs, devices))
    {
        // 保留失败 collector 供 DFX（LoadInfo/符号/使能原因在 GetDcmiStatus 中可查）
        dcmiCollector_ = std::move(collector);
        LOG(WARNING) << "StartDcmi: DcmiCollector start failed, DCMI metrics disabled";
        return false;
    }
    dcmiCollector_ = std::move(collector);
    return true;
}

void Monitor::StopDcmi()
{
    // 仅停止采集线程，保留 collector 对象：Stop 后 GetDcmiStatus 仍能读到
    // 本次会话的加载状态、tick/超时/失败计数与各接口耗时（DFX 诊断关键）。
    // 下次 StartDcmi 会先停旧线程再整体替换。
    if (dcmiCollector_ != nullptr)
    {
        dcmiCollector_->Stop();
    }
}

void Monitor::Stop()
{
    auto msptiMonitor = MsptiMonitor::GetInstance();
    bool msptiRunning = msptiMonitor->IsStarted();
    if (msptiRunning)
    {
        msptiMonitor->Stop();
    }
    bool dcmiRunning = dcmiCollector_ != nullptr && dcmiCollector_->IsRunning();
    StopDcmi();
    if (!msptiRunning && !dcmiRunning)
    {
        LOG(WARNING) << "monitor not started";
        return;
    }
    LOG(INFO) << "monitor stopped";
}

void Monitor::Clear()
{
    apiData_.clear();
    aclApiData_.clear();
    nodeApiData_.clear();
    runtimeApiData_.clear();
    kernelData_.clear();
    communicationData_.clear();
    markerData_.clear();
    kinds_.clear();

    apiData_.reserve(DEFAULT_CAPACITY);
    aclApiData_.reserve(DEFAULT_CAPACITY);
    nodeApiData_.reserve(DEFAULT_CAPACITY);
    runtimeApiData_.reserve(DEFAULT_CAPACITY);
    kernelData_.reserve(DEFAULT_CAPACITY);
    communicationData_.reserve(DEFAULT_CAPACITY);
    markerData_.reserve(DEFAULT_CAPACITY);
    ClearDcmiData();
}

void Monitor::ClearDcmiData()
{
    std::lock_guard<std::mutex> lock(dcmiMutex_);
    dcmiRing_.Clear();
    dcmiDropCount_ = 0;
}

void Monitor::ClearDcmiDataForTest() { ClearDcmiData(); }

void Monitor::ReportAPIData(API &&api, msptiActivityKind kind)
{
    std::lock_guard<std::mutex> lock(apiMutex_);
    switch (kind)
    {
        case msptiActivityKind::MSPTI_ACTIVITY_KIND_API:
            apiData_.emplace_back(std::move(api));
            break;
        case msptiActivityKind::MSPTI_ACTIVITY_KIND_ACL_API:
            aclApiData_.emplace_back(std::move(api));
            break;
        case msptiActivityKind::MSPTI_ACTIVITY_KIND_NODE_API:
            nodeApiData_.emplace_back(std::move(api));
            break;
        case msptiActivityKind::MSPTI_ACTIVITY_KIND_RUNTIME_API:
            runtimeApiData_.emplace_back(std::move(api));
            break;
        default:
            LOG(WARNING) << "Not supported MsptiActivityKind: " << kind;
            break;
    }
}

void Monitor::ReportKernelData(Kernel &&kernel)
{
    std::lock_guard<std::mutex> lock(kernelMutex_);
    kernelData_.emplace_back(std::move(kernel));
}

void Monitor::ReportCommunicationData(Communication &&communication)
{
    std::lock_guard<std::mutex> lock(communicationMutex_);
    communicationData_.emplace_back(std::move(communication));
}

void Monitor::ReportMarkerData(Marker &&marker)
{
    std::lock_guard<std::mutex> lock(markerMutex_);
    markerData_.emplace_back(std::move(marker));
}

void Monitor::ReportDcmiData(DcmiSample &&sample)
{
    std::lock_guard<std::mutex> lock(dcmiMutex_);
    if (dcmiRing_.Push(std::move(sample)))
    {
        dcmiDropCount_++;
    }
}

std::vector<DcmiSample> Monitor::GetDcmiData() const
{
    std::lock_guard<std::mutex> lock(dcmiMutex_);
    return dcmiRing_.Snapshot();
}

DcmiStatusSnapshot Monitor::GetDcmiStatus() const
{
    DcmiStatusSnapshot snap;
    snap.meta = GetDcmiMetricMeta();
    {
        std::lock_guard<std::mutex> lock(dcmiMutex_);
        snap.ringSize = dcmiRing_.Size();
        snap.ringCapacity = dcmiRing_.Capacity();
        snap.ringDropCount = dcmiDropCount_;
    }
    if (dcmiCollector_ != nullptr)
    {
        snap.load = dcmiCollector_->LoadInfo();
        const auto &kindStatus = dcmiCollector_->KindStatus();
        snap.kinds.reserve(kindStatus.size());
        for (const auto &entry : kindStatus)
        {
            snap.kinds.emplace_back(entry.first, entry.second);
        }
        std::sort(
            snap.kinds.begin(), snap.kinds.end(),
            [](const std::pair<DcmiMetricKind, DcmiKindStatus> &a, const std::pair<DcmiMetricKind, DcmiKindStatus> &b)
            { return static_cast<int32_t>(a.first) < static_cast<int32_t>(b.first); });
        auto st = dcmiCollector_->GetStatus();
        snap.tickCount = st.tickCount;
        snap.overrunCount = st.overrunCount;
        snap.lastTickCollectUs = st.lastTickCollectUs;
        snap.groupCallCounts = std::move(st.groupCallCounts);
        snap.groupCollectNs = std::move(st.groupCollectNs);
        snap.groupFailCounts = std::move(st.groupFailCounts);
        snap.lastErrorCodes = std::move(st.lastErrorCodes);
    }
    else
    {
        snap.load.status = DcmiApiStatus::NOT_SUPPORT;
        snap.load.error = "dcmi collector not started";
    }
    return snap;
}

std::vector<DcmiMetricMeta> Monitor::GetDcmiMetricMeta() const
{
    return dcmi::DcmiMetricRegistry::GetInstance()->AllMeta();
}

}  // namespace monitor
}  // namespace ipc_monitor
}  // namespace dynolog_npu
