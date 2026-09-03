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

#include "dcmi/DcmiCollector.h"

#include <glog/logging.h>

#include <algorithm>
#include <chrono>
#include <thread>

#include "dcmi/DcmiMetricRegistry.h"
#include "monitor/Monitor.h"
#include "utils/utils.h"

namespace dynolog_npu
{
namespace ipc_monitor
{
namespace dcmi
{

namespace
{
constexpr uint64_t DEFAULT_INTERVAL_NS = 10000000ULL;  // 10ms（与 Python 默认一致）
constexpr uint64_t SLEEP_CHUNK_NS = 1000000ULL;        // 分段睡眠 ≤1ms，保证 Stop 及时
constexpr uint64_t ERROR_LOG_INTERVAL = 1000;          // 同类错误日志限流：首次 + 每 1000 次
}  // namespace

DcmiCollector::DcmiCollector() { SetThreadName("DcmiCollector"); }

DcmiCollector::~DcmiCollector() { Stop(); }

bool DcmiCollector::PrepareMetrics(const std::set<monitor::DcmiMetricKind> &enabled)
{
    auto *registry = DcmiMetricRegistry::GetInstance();
    metrics_ = registry->Create(enabled);

    DcmiProbeDevice probe;
    if (!devices_.empty())
    {
        probe.cardId = std::get<1>(devices_[0]);
        probe.devId = std::get<2>(devices_[0]);
        probe.flatDevId = std::get<0>(devices_[0]);
    }

    // Init 失败的 group 直接移除：即便其 kind 后被兜底接管，死 group 也不空转/计假失败
    std::unordered_set<std::string> deadGroups;
    for (auto &m : metrics_)
    {
        if (!m->Init(api_, enabled, probe, kindStatus_))
        {
            deadGroups.insert(m->GroupName());
            LOG(WARNING) << "DcmiCollector: group " << m->GroupName() << " init failed, skipped";
        }
    }

    // AICORE 利用率族未被激活（如 multi_utilization 缺失）→ UtilRateMetric 按 type 兜底
    bool coreUtilActive = false;
    const monitor::DcmiMetricKind kCoreUtils[] = {
        monitor::DcmiMetricKind::AICoreUtil, monitor::DcmiMetricKind::AICubeUtil,
        monitor::DcmiMetricKind::VectorCoreUtil, monitor::DcmiMetricKind::NPUUtil};
    for (auto kind : kCoreUtils)
    {
        auto it = kindStatus_.find(kind);
        if (it != kindStatus_.end() && it->second.enabled)
        {
            coreUtilActive = true;
            break;
        }
    }
    if (!coreUtilActive)
    {
        auto fallback = registry->CreateUtilRate(enabled);
        if (fallback != nullptr && fallback->Init(api_, enabled, probe, kindStatus_))
        {
            metrics_.push_back(std::move(fallback));
        }
    }

    metrics_.erase(
        std::remove_if(metrics_.begin(), metrics_.end(), [&deadGroups](const std::unique_ptr<DcmiMetricBase> &m)
                       { return deadGroups.count(m->GroupName()) > 0; }),
        metrics_.end());

    // 启动提示：列出当前设备不可采集的指标及原因（执行期 DFX，一次性）
    std::string disabled;
    for (const auto &entry : kindStatus_)
    {
        if (!entry.second.enabled)
        {
            const auto *meta = registry->FindMeta(entry.first);
            disabled += (meta != nullptr ? meta->displayName : "kind" + std::to_string(static_cast<int>(entry.first))) +
                        "(" + entry.second.reason + "); ";
        }
    }
    LOG_IF(WARNING, !disabled.empty()) << "DcmiCollector: metrics not collectable on this device: " << disabled;

    for (const auto &entry : kindStatus_)
    {
        if (entry.second.enabled)
        {
            return true;
        }
    }
    LOG(WARNING) << "DcmiCollector: no dcmi metric enabled";
    return false;
}

bool DcmiCollector::PrepareDevices(const std::vector<uint32_t> &devices)
{
    devices_.clear();
    wantAutoNarrow_ = false;
    if (!api_.Available())
    {
        return false;
    }
    if (devices.empty())
    {
        // 默认只采当前进程在用的 device；取不到回退全卡并开启自动收敛
        int32_t cur = -1;
        if (DcmiApiLoader::GetCurrentDevice(cur))
        {
            int cardId = 0;
            int dcmiDevId = 0;
            if (api_.MapDevId(static_cast<uint32_t>(cur), cardId, dcmiDevId))
            {
                devices_.emplace_back(static_cast<uint32_t>(cur), cardId, dcmiDevId);
                LOG(INFO) << "DcmiCollector: auto-detected current device=" << cur;
            }
        }
        if (devices_.empty())
        {
            LOG(WARNING) << "DcmiCollector: GetCurrentDevice unavailable, fallback to all devices";
            for (uint32_t flat : api_.ProbeAllDevices())
            {
                int cardId = 0;
                int dcmiDevId = 0;
                if (api_.MapDevId(flat, cardId, dcmiDevId))
                {
                    devices_.emplace_back(flat, cardId, dcmiDevId);
                }
            }
            wantAutoNarrow_ = true;
        }
    }
    else
    {
        for (uint32_t flat : devices)
        {
            int cardId = 0;
            int dcmiDevId = 0;
            if (api_.MapDevId(flat, cardId, dcmiDevId))
            {
                devices_.emplace_back(flat, cardId, dcmiDevId);
            }
            else
            {
                LOG(WARNING) << "DcmiCollector: MapDevId failed for flatDevId=" << flat << ", skipped";
            }
        }
    }
    if (devices_.empty())
    {
        LOG(WARNING) << "DcmiCollector: no device to collect";
        return false;
    }
    std::vector<uint32_t> flats;
    flats.reserve(devices_.size());
    for (const auto &dev : devices_)
    {
        flats.push_back(std::get<0>(dev));
    }
    api_.SetDevices(flats);
    LOG(INFO) << "DcmiCollector: device count=" << devices_.size();
    return true;
}

bool DcmiCollector::Start(const std::set<monitor::DcmiMetricKind> &metrics, uint32_t intervalMs,
                          const std::vector<uint32_t> &devices)
{
    if (running_.load())
    {
        LOG(WARNING) << "DcmiCollector already running";
        return false;
    }
    if (api_.Init() != monitor::DcmiApiStatus::OK)
    {
        LOG(WARNING) << "DcmiCollector: api init failed, DCMI metrics disabled: " << api_.LoadInfo().error;
        return false;
    }
    intervalNs_ = static_cast<uint64_t>(intervalMs) * 1000000ULL;
    if (intervalNs_ == 0)
    {
        intervalNs_ = DEFAULT_INTERVAL_NS;
    }
    kindStatus_.clear();
    if (!PrepareDevices(devices) || !PrepareMetrics(metrics))
    {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(statusMutex_);
        tickCount_ = 0;
        overrunCount_ = 0;
        lastTickCollectUs_ = 0;
        groupCallCounts_.clear();
        groupCollectNs_.clear();
        groupFailCounts_.clear();
        lastErrorCodes_.clear();
        errLogState_.clear();
    }
    running_.store(true);
    return Thread::Start() == 0;
}

void DcmiCollector::Stop()
{
    if (!running_.load() && !finalTickRequested_.load())
    {
        return;
    }
    running_.store(false);
    finalTickRequested_.store(true);  // Run 补一拍最终采样后退出
    Thread::Stop();
    {
        std::lock_guard<std::mutex> lock(statusMutex_);
        LOG(INFO) << "DcmiCollector stopped: ticks=" << tickCount_ << " overruns=" << overrunCount_
                  << " lastTickCollectUs=" << lastTickCollectUs_;
    }
}

DcmiCollector::Status DcmiCollector::GetStatus() const
{
    std::lock_guard<std::mutex> lock(statusMutex_);
    Status st;
    st.running = running_.load();
    st.tickCount = tickCount_;
    st.overrunCount = overrunCount_;
    st.lastTickCollectUs = lastTickCollectUs_;
    for (const auto &entry : groupCallCounts_)
    {
        st.groupCallCounts.emplace_back(entry.first, entry.second);
    }
    for (const auto &entry : groupCollectNs_)
    {
        st.groupCollectNs.emplace_back(entry.first, entry.second);
    }
    for (const auto &entry : groupFailCounts_)
    {
        st.groupFailCounts.emplace_back(entry.first, entry.second);
    }
    for (const auto &entry : lastErrorCodes_)
    {
        st.lastErrorCodes.emplace_back(entry.first, entry.second);
    }
    return st;
}

void DcmiCollector::CollectTick(uint64_t tickNo, DcmiSampleSink &out,
                                std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> &tickStats)
{
    for (const auto &dev : devices_)
    {
        uint32_t flatDevId = std::get<0>(dev);
        int cardId = std::get<1>(dev);
        int dcmiDevId = std::get<2>(dev);
        for (auto &m : metrics_)
        {
            // 慢接口节流；最终采样拍跳过门控，保证停止时刻全部指标都采到
            if (m->SampleEveryTicks() > 1 && tickNo % m->SampleEveryTicks() != 0 && !finalTickRequested_.load())
            {
                continue;
            }
            uint64_t gStart = getCurrentTimestamp64();
            DcmiSampleSink sink;
            int ret = m->Collect(cardId, dcmiDevId, flatDevId, 0, sink);
            uint64_t gEnd = getCurrentTimestamp64();
            auto &stat = tickStats[m->GroupName()];
            stat.first++;
            stat.second += (gEnd - gStart);
            if (ret != 0)
            {
                RecordFail(m->GroupName(), ret);
            }
            for (auto &sample : sink)
            {
                out.push_back(std::move(sample));
            }
        }
    }
}

void DcmiCollector::RecordFail(const std::string &group, int errCode)
{
    {
        std::lock_guard<std::mutex> lock(statusMutex_);
        groupFailCounts_[group]++;
        lastErrorCodes_[group] = errCode;
    }
    // 错误日志限流：首次 + 每 ERROR_LOG_INTERVAL 次同类错误（errLogState_ 仅 Run 线程访问）
    auto &state = errLogState_[group];
    if (state.first == errCode)
    {
        state.second++;
        if (state.second == 1 || state.second % ERROR_LOG_INTERVAL == 0)
        {
            LOG(WARNING) << "DcmiCollector: group " << group << " call failed, ret=" << errCode
                         << " (count=" << state.second << ")";
        }
    }
    else
    {
        state.first = errCode;
        state.second = 1;
        LOG(WARNING) << "DcmiCollector: group " << group << " call failed, ret=" << errCode;
    }
}

void DcmiCollector::Run()
{
    LOG(INFO) << "DcmiCollector thread started, intervalNs=" << intervalNs_ << " metrics=" << metrics_.size()
              << " devices=" << devices_.size();
    uint64_t nextNs = getCurrentTimestamp64();
    uint64_t tickNo = 0;
    while (running_.load() || finalTickRequested_.load())
    {
        // 自动收敛：回退全卡模式下每拍重试 aclrtGetDevice，取到当前设备即收敛为单卡
        if (wantAutoNarrow_)
        {
            int32_t cur = -1;
            if (DcmiApiLoader::GetCurrentDevice(cur))
            {
                int cardId = 0;
                int dcmiDevId = 0;
                if (api_.MapDevId(static_cast<uint32_t>(cur), cardId, dcmiDevId))
                {
                    devices_.clear();
                    devices_.emplace_back(static_cast<uint32_t>(cur), cardId, dcmiDevId);
                    wantAutoNarrow_ = false;
                    LOG(INFO) << "DcmiCollector: narrowed to current device=" << cur;
                }
            }
        }
        uint64_t curNs = getCurrentTimestamp64();
        if (curNs < nextNs && !finalTickRequested_.load())
        {
            std::this_thread::sleep_for(std::chrono::nanoseconds(std::min<uint64_t>(nextNs - curNs, SLEEP_CHUNK_NS)));
            continue;
        }
        uint64_t tickStartNs = getCurrentTimestamp64();
        bool overrun = (tickStartNs > nextNs);
        bool isFinalTick = finalTickRequested_.load();
        DcmiSampleSink tickSamples;
        std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> tickStats;
        CollectTick(tickNo, tickSamples, tickStats);
        uint64_t afterCollectNs = getCurrentTimestamp64();
        {
            std::lock_guard<std::mutex> lock(statusMutex_);
            tickCount_++;
            if (overrun)
            {
                overrunCount_++;
            }
            lastTickCollectUs_ = (afterCollectNs - tickStartNs) / 1000;
            for (const auto &entry : tickStats)
            {
                groupCallCounts_[entry.first] += entry.second.first;
                groupCollectNs_[entry.first] += entry.second.second;
            }
        }
        // 样本时间戳：普通拍取节拍开始（同拍各指标起点对齐）；最终拍取采集完成时刻，
        // 否则慢接口（utilization_rate 可达百 ms 级）会让最后样本早于真实停止时刻。
        // Stop 可能在采集期间置位 finalTickRequested_（本拍 isFinalTick 读于节拍开始），
        // 以采集完成后的状态为准再判一次，保证被 Stop 打断的一拍也按最终拍盖章
        auto *mon = monitor::Monitor::GetInstance();
        uint64_t stampNs = (isFinalTick || finalTickRequested_.load()) ? afterCollectNs : tickStartNs;
        for (auto &sample : tickSamples)
        {
            sample.timestampNs = stampNs;
            mon->ReportDcmiData(std::move(sample));
        }
        // 不追赶：超时后下一拍从当前时刻+interval 起算，避免突发补采
        nextNs = std::max(afterCollectNs + intervalNs_, nextNs + intervalNs_);
        tickNo++;
        // 仅本拍以最终拍开始（isFinalTick）才清标志退出；若 Stop 在本拍采集期间才置位
        // （isFinalTick=false 但标志已 true），本拍可能已跳过部分节流指标，保留标志，
        // 下一拍以完整最终拍补采全部指标后退出
        if (isFinalTick)
        {
            finalTickRequested_.store(false);
        }
    }
    LOG(INFO) << "DcmiCollector thread exiting";
}

}  // namespace dcmi
}  // namespace ipc_monitor
}  // namespace dynolog_npu
