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

#ifndef MSMONITOR_DCMI_METRIC_BASE_H
#define MSMONITOR_DCMI_METRIC_BASE_H

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "dcmi/DcmiApiLoader.h"
#include "dcmi/DcmiTypes.h"

namespace dynolog_npu
{
namespace ipc_monitor
{
namespace dcmi
{

// 单次采集节拍内一个 group 产出的样本容器
using DcmiSampleSink = std::vector<monitor::DcmiSample>;

// 首设备 probe 用（Init 阶段校验符号与首次调用）
struct DcmiProbeDevice
{
    int cardId = -1;
    int devId = -1;
    uint32_t flatDevId = 0;
};

// DCMI 指标采集抽象（策略模式 + 分组批量）。
// 一个 group = 一次 DCMI 调用产出多个 kind；代际差异(v1/v2)由 DcmiApiLoader 访问器屏蔽，
// metric 层只面向统一访问器，新增指标/代际均无需改动 metric 之外的模块。
class DcmiMetricBase
{
   public:
    virtual ~DcmiMetricBase() = default;

    virtual std::string GroupName() const = 0;
    virtual std::vector<monitor::DcmiMetricKind> Provides() const = 0;

    // 校验符号并首设备 probe；status 记录各 kind 使能与原因(DFX)。返回是否至少一个 kind 可用。
    virtual bool Init(const DcmiApiLoader &api, const std::set<monitor::DcmiMetricKind> &enabled,
                      const DcmiProbeDevice &probe,
                      std::unordered_map<monitor::DcmiMetricKind, monitor::DcmiKindStatus> &status) = 0;

    // 采样节流：每 N 个节拍采集一次（慢接口覆写降频）
    virtual uint32_t SampleEveryTicks() const { return 1; }

    // 采集：填充 sink。返回 0 成功，非 0 为 DCMI 返回码（供 DFX 计数/限流日志）
    virtual int Collect(int cardId, int dcmiDevId, uint32_t flatDevId, uint64_t tsNs, DcmiSampleSink &sink) = 0;
};

}  // namespace dcmi
}  // namespace ipc_monitor
}  // namespace dynolog_npu

#endif  // MSMONITOR_DCMI_METRIC_BASE_H
