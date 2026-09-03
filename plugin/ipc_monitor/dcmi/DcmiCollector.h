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

#ifndef MSMONITOR_DCMI_COLLECTOR_H
#define MSMONITOR_DCMI_COLLECTOR_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "dcmi/DcmiApiLoader.h"
#include "dcmi/DcmiMetricBase.h"
#include "thread.h"

namespace dynolog_npu
{
namespace ipc_monitor
{
namespace dcmi
{

using DcmiDevice = std::tuple<uint32_t, int, int>;  // (flatDevId, cardId, dcmiDevId)

// DCMI 采集线程：单线程按节拍轮询设备×metric group，样本写入 Monitor 的 ring。
// DFX：节拍/超时计数、各接口耗时与失败计数、错误日志限流、停止汇总。
class DcmiCollector : public Thread
{
   public:
    DcmiCollector();
    ~DcmiCollector();

    bool Start(const std::set<monitor::DcmiMetricKind> &metrics, uint32_t intervalMs,
               const std::vector<uint32_t> &devices);
    void Stop();  // 幂等；停止时补一次最终采样（覆盖 stop 前最后时刻）
    bool IsRunning() const { return running_.load(); }

    struct Status
    {
        bool running = false;
        uint64_t tickCount = 0;
        uint64_t overrunCount = 0;
        uint64_t lastTickCollectUs = 0;  // 最近一节拍采集耗时，诊断慢接口/采样率
        std::vector<std::pair<std::string, uint64_t>> groupCallCounts;
        std::vector<std::pair<std::string, uint64_t>> groupCollectNs;
        std::vector<std::pair<std::string, uint64_t>> groupFailCounts;
        std::vector<std::pair<std::string, int>> lastErrorCodes;
    };
    Status GetStatus() const;

    const monitor::DcmiLoadInfo &LoadInfo() const { return api_.LoadInfo(); }
    const std::unordered_map<monitor::DcmiMetricKind, monitor::DcmiKindStatus> &KindStatus() const
    {
        return kindStatus_;
    }
    DcmiApiLoader &ApiForTest() { return api_; }  // UT 注入用

   private:
    void Run() override;
    bool PrepareMetrics(const std::set<monitor::DcmiMetricKind> &enabled);
    bool PrepareDevices(const std::vector<uint32_t> &devices);
    void CollectTick(uint64_t tickNo, DcmiSampleSink &out,
                     std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> &tickStats);
    void RecordFail(const std::string &group, int errCode);

    DcmiApiLoader api_;
    std::vector<std::unique_ptr<DcmiMetricBase>> metrics_;
    std::vector<DcmiDevice> devices_;
    std::unordered_map<monitor::DcmiMetricKind, monitor::DcmiKindStatus> kindStatus_;
    std::atomic<bool> running_{false};
    std::atomic<bool> finalTickRequested_{false};  // 停止时补最终采样
    uint64_t intervalNs_ = 10000000;               // 10ms 默认
    bool wantAutoNarrow_ = false;                  // 回退全卡后每拍重试 aclrtGetDevice 收敛单卡

    mutable std::mutex statusMutex_;
    uint64_t tickCount_ = 0;
    uint64_t overrunCount_ = 0;
    uint64_t lastTickCollectUs_ = 0;
    std::unordered_map<std::string, uint64_t> groupCallCounts_;
    std::unordered_map<std::string, uint64_t> groupCollectNs_;
    std::unordered_map<std::string, uint64_t> groupFailCounts_;
    std::unordered_map<std::string, int> lastErrorCodes_;
    std::unordered_map<std::string, std::pair<int, uint64_t>> errLogState_;  // 错误日志限流
};

}  // namespace dcmi
}  // namespace ipc_monitor
}  // namespace dynolog_npu

#endif  // MSMONITOR_DCMI_COLLECTOR_H
