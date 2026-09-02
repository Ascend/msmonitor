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

#ifndef MSMONITOR_DCMI_TYPES_H
#define MSMONITOR_DCMI_TYPES_H

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace py = pybind11;

namespace dynolog_npu
{
namespace ipc_monitor
{
namespace monitor
{

// 硬件层：Python 配置 DCMI 采集的第一级单位，选层即采该层全部指标
enum class DcmiLayer : int32_t
{
    DEVICE = 0,  // Power / Temp
    AICORE = 1,  // AICore 频率/利用率族
    AICPU = 2,   // AICPU 最大/当前频率、利用率
    HBM = 3,     // 片上内存频率/容量/用量/带宽/温度
};

// 层内细粒度指标（Python dcmi_metrics 精调）；新增指标在注册表加一行即可
enum class DcmiMetricKind : int32_t
{
    Power = 0,
    Temp = 1,
    AICoreFreq = 2,
    AICoreRatedFreq = 3,
    AICoreUtil = 4,
    AICubeUtil = 5,
    VectorCoreUtil = 6,
    NPUUtil = 7,
    AICPUMaxFreq = 8,
    AICPUFreq = 9,
    AICPUUtil = 10,
    HBMFreq = 11,
    HBMMemUsed = 12,
    HBMMemTotal = 13,
    HBMBandwidth = 14,
    HBMTemp = 15,
    Voltage = 16,  // 预留
};

struct DcmiSample
{
    DcmiMetricKind kind;
    uint64_t timestampNs;
    int32_t deviceId;  // 扁平 devId
    double value;
    py::tuple to_tuple() const { return py::make_tuple(static_cast<int32_t>(kind), timestampNs, deviceId, value); }
};

// 指标元数据：注册表维护（层/展示名/单位），Python 导出与 DFX 的单一数据源
struct DcmiMetricMeta
{
    DcmiMetricKind kind;
    DcmiLayer layer;
    std::string name;         // 枚举名
    std::string displayName;  // 展示名
    std::string unit;         // 单位
};

enum class DcmiApiStatus : int32_t
{
    OK = 0,
    NOT_SUPPORT = 1,
    FAILED = 2
};

// 加载/采集诊断（DFX）：so 加载、符号、init、拓扑结果
struct DcmiLoadInfo
{
    DcmiApiStatus status = DcmiApiStatus::NOT_SUPPORT;
    std::string version;                      // "V1" / "V2"（950 等 v2-only 代际）
    std::string libPath;                      // 命中的 so 路径
    std::vector<std::string> missingSymbols;  // 缺失符号
    int initRet = -1;                         // init 返回码
    int cardNum = 0;
    int deviceNumPerCard = 0;       // 每卡 NPU 芯片数（A3=2、A2/A5=1）
    std::vector<uint32_t> devices;  // 实际采集设备
    std::string error;              // 可读原因
};

// 单指标使能/失败原因（DFX）
struct DcmiKindStatus
{
    bool enabled = false;
    std::string reason;
};

// Monitor::GetDcmiStatus() 返回快照（bindings 转 py::dict；Stop 后仍可读用于诊断）
struct DcmiStatusSnapshot
{
    DcmiLoadInfo load;
    std::vector<DcmiMetricMeta> meta;
    std::vector<std::pair<DcmiMetricKind, DcmiKindStatus>> kinds;
    uint64_t tickCount = 0;
    uint64_t overrunCount = 0;
    uint64_t lastTickCollectUs = 0;  // 最近一节拍采集耗时
    std::vector<std::pair<std::string, uint64_t>> groupCallCounts;
    std::vector<std::pair<std::string, uint64_t>> groupCollectNs;
    uint64_t ringDropCount = 0;
    uint64_t ringSize = 0;
    uint64_t ringCapacity = 0;
    std::vector<std::pair<std::string, uint64_t>> groupFailCounts;
    std::vector<std::pair<std::string, int>> lastErrorCodes;
};

}  // namespace monitor
}  // namespace ipc_monitor
}  // namespace dynolog_npu

#endif  // MSMONITOR_DCMI_TYPES_H
