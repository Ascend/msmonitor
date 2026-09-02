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

#include "dcmi/DcmiMetricRegistry.h"

#include <algorithm>
#include <unordered_set>

#include "dcmi/DcmiMetrics.h"

namespace dynolog_npu
{
namespace ipc_monitor
{
namespace dcmi
{

DcmiMetricRegistry::DcmiMetricRegistry() { RegisterDefaults(); }

void DcmiMetricRegistry::Register(monitor::DcmiMetricKind kind, Factory factory)
{
    factories_[kind] = std::move(factory);
}

void DcmiMetricRegistry::RegisterMeta(monitor::DcmiMetricKind kind, monitor::DcmiLayer layer, const std::string &name,
                                      const std::string &displayName, const std::string &unit)
{
    meta_[kind] = monitor::DcmiMetricMeta{kind, layer, name, displayName, unit};
}

namespace
{
template <typename MetricT>
std::unique_ptr<DcmiMetricBase> MakeMetric()
{
    return std::make_unique<MetricT>();
}

// UtilRateMetric 可提供的 kind（AICORE 利用率兜底时按此过滤）
bool IsUtilRateKind(monitor::DcmiMetricKind kind)
{
    switch (kind)
    {
        case monitor::DcmiMetricKind::AICPUUtil:
        case monitor::DcmiMetricKind::AICoreUtil:
        case monitor::DcmiMetricKind::AICubeUtil:
        case monitor::DcmiMetricKind::VectorCoreUtil:
        case monitor::DcmiMetricKind::NPUUtil:
            return true;
        default:
            return false;
    }
}
}  // namespace

// 内置指标定义表：新增指标 = 追加一行（kind/layer/名称/单位/所属 group 工厂）
void DcmiMetricRegistry::RegisterDefaults()
{
    struct Def
    {
        monitor::DcmiMetricKind kind;
        monitor::DcmiLayer layer;
        const char *name;
        const char *displayName;
        const char *unit;
        Factory factory;
    };
    const Def defs[] = {
        {monitor::DcmiMetricKind::Power, monitor::DcmiLayer::DEVICE, "Power", "Power", "W", MakeMetric<PowerMetric>},
        {monitor::DcmiMetricKind::Temp, monitor::DcmiLayer::DEVICE, "Temp", "Temperature", "\u2103",
         MakeMetric<TempMetric>},
        {monitor::DcmiMetricKind::AICoreFreq, monitor::DcmiLayer::AICORE, "AICoreFreq", "AICore Freq", "MHz",
         MakeMetric<AicoreInfoMetric>},
        {monitor::DcmiMetricKind::AICoreRatedFreq, monitor::DcmiLayer::AICORE, "AICoreRatedFreq", "AICore Rated Freq",
         "MHz", MakeMetric<AicoreInfoMetric>},
        {monitor::DcmiMetricKind::AICoreUtil, monitor::DcmiLayer::AICORE, "AICoreUtil", "AICore Utilization", "%",
         MakeMetric<MultiUtilMetric>},
        {monitor::DcmiMetricKind::AICubeUtil, monitor::DcmiLayer::AICORE, "AICubeUtil", "AI Cube Utilization", "%",
         MakeMetric<MultiUtilMetric>},
        {monitor::DcmiMetricKind::VectorCoreUtil, monitor::DcmiLayer::AICORE, "VectorCoreUtil",
         "Vector Core Utilization", "%", MakeMetric<MultiUtilMetric>},
        {monitor::DcmiMetricKind::NPUUtil, monitor::DcmiLayer::AICORE, "NPUUtil", "NPU Utilization", "%",
         MakeMetric<MultiUtilMetric>},
        {monitor::DcmiMetricKind::AICPUMaxFreq, monitor::DcmiLayer::AICPU, "AICPUMaxFreq", "AICPU Max Freq", "MHz",
         MakeMetric<AicpuInfoMetric>},
        {monitor::DcmiMetricKind::AICPUFreq, monitor::DcmiLayer::AICPU, "AICPUFreq", "AICPU Freq", "MHz",
         MakeMetric<AicpuInfoMetric>},
        {monitor::DcmiMetricKind::AICPUUtil, monitor::DcmiLayer::AICPU, "AICPUUtil", "AICPU Utilization", "%",
         MakeMetric<UtilRateMetric>},
        {monitor::DcmiMetricKind::HBMFreq, monitor::DcmiLayer::HBM, "HBMFreq", "HBM Freq", "MHz",
         MakeMetric<HbmInfoMetric>},
        {monitor::DcmiMetricKind::HBMMemUsed, monitor::DcmiLayer::HBM, "HBMMemUsed", "HBM Memory Used", "MB",
         MakeMetric<HbmInfoMetric>},
        {monitor::DcmiMetricKind::HBMMemTotal, monitor::DcmiLayer::HBM, "HBMMemTotal", "HBM Memory Total", "MB",
         MakeMetric<HbmInfoMetric>},
        {monitor::DcmiMetricKind::HBMBandwidth, monitor::DcmiLayer::HBM, "HBMBandwidth", "HBM Bandwidth Utilization",
         "%", MakeMetric<HbmInfoMetric>},
        {monitor::DcmiMetricKind::HBMTemp, monitor::DcmiLayer::HBM, "HBMTemp", "HBM Temperature", "\u2103",
         MakeMetric<HbmInfoMetric>},
    };
    for (const auto &d : defs)
    {
        Register(d.kind, d.factory);
        RegisterMeta(d.kind, d.layer, d.name, d.displayName, d.unit);
    }
}

std::vector<std::unique_ptr<DcmiMetricBase>> DcmiMetricRegistry::Create(
    const std::set<monitor::DcmiMetricKind> &enabled) const
{
    std::vector<std::unique_ptr<DcmiMetricBase>> out;
    std::unordered_set<std::string> seenGroups;
    for (auto kind : enabled)
    {
        auto it = factories_.find(kind);
        if (it == factories_.end())
        {
            continue;
        }
        auto metric = it->second();
        if (metric == nullptr)
        {
            continue;
        }
        if (!seenGroups.insert(metric->GroupName()).second)
        {
            continue;  // 同一 group 只实例化一次（多 kind 共享一次 DCMI 调用）
        }
        out.push_back(std::move(metric));
    }
    return out;
}

std::set<monitor::DcmiMetricKind> DcmiMetricRegistry::KindsForLayer(monitor::DcmiLayer layer) const
{
    // 仅返回该层"可得"指标（有元数据且有 group 工厂）
    std::set<monitor::DcmiMetricKind> out;
    for (const auto &entry : meta_)
    {
        if (entry.second.layer == layer && factories_.count(entry.first) > 0)
        {
            out.insert(entry.first);
        }
    }
    return out;
}

std::unique_ptr<DcmiMetricBase> DcmiMetricRegistry::CreateUtilRate(
    const std::set<monitor::DcmiMetricKind> &enabled) const
{
    std::set<monitor::DcmiMetricKind> subset;
    for (auto kind : enabled)
    {
        if (IsUtilRateKind(kind) && meta_.count(kind) > 0)
        {
            subset.insert(kind);
        }
    }
    if (subset.empty())
    {
        return nullptr;
    }
    return std::make_unique<UtilRateMetric>();
}

const monitor::DcmiMetricMeta *DcmiMetricRegistry::FindMeta(monitor::DcmiMetricKind kind) const
{
    auto it = meta_.find(kind);
    return it == meta_.end() ? nullptr : &it->second;
}

std::vector<monitor::DcmiMetricMeta> DcmiMetricRegistry::AllMeta() const
{
    std::vector<monitor::DcmiMetricMeta> out;
    out.reserve(meta_.size());
    for (const auto &entry : meta_)
    {
        out.push_back(entry.second);
    }
    std::sort(out.begin(), out.end(), [](const monitor::DcmiMetricMeta &a, const monitor::DcmiMetricMeta &b)
              { return static_cast<int32_t>(a.kind) < static_cast<int32_t>(b.kind); });
    return out;
}

}  // namespace dcmi
}  // namespace ipc_monitor
}  // namespace dynolog_npu
