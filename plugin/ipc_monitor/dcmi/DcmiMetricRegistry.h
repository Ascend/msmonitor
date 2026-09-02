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

#ifndef MSMONITOR_DCMI_METRIC_REGISTRY_H
#define MSMONITOR_DCMI_METRIC_REGISTRY_H

#include <functional>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

#include "dcmi/DcmiMetricBase.h"
#include "singleton.h"

namespace dynolog_npu
{
namespace ipc_monitor
{
namespace dcmi
{

// DCMI 指标注册表（扩展点）。单例。
// 职责：
//  - kind → group 工厂（多 kind 可共享同一 group）
//  - kind → 元数据（layer/name/displayName/unit），作为导出与 DFX 的单一数据源
//  - 层 → kind 集合（Python 按层配置的展开）
class DcmiMetricRegistry : public Singleton<DcmiMetricRegistry>
{
    friend class Singleton<DcmiMetricRegistry>;

   public:
    using Factory = std::function<std::unique_ptr<DcmiMetricBase>()>;

    DcmiMetricRegistry();

    void Register(monitor::DcmiMetricKind kind, Factory factory);
    void RegisterMeta(monitor::DcmiMetricKind kind, monitor::DcmiLayer layer, const std::string &name,
                      const std::string &displayName, const std::string &unit);

    // 按 enabled 集合创建 group 实例（同一 group 只实例化一次）。
    std::vector<std::unique_ptr<DcmiMetricBase>> Create(const std::set<monitor::DcmiMetricKind> &enabled) const;

    // 指定层的全部指标集合（默认=该层全部可得指标）。
    std::set<monitor::DcmiMetricKind> KindsForLayer(monitor::DcmiLayer layer) const;

    // 创建 UtilRateMetric 兜底实例（multi_utilization 不可用时接管 AICORE 利用率 kind）。
    std::unique_ptr<DcmiMetricBase> CreateUtilRate(const std::set<monitor::DcmiMetricKind> &enabled) const;

    const monitor::DcmiMetricMeta *FindMeta(monitor::DcmiMetricKind kind) const;
    std::vector<monitor::DcmiMetricMeta> AllMeta() const;

    // 注册内置默认指标（各层）。幂等；构造函数自动调用。
    void RegisterDefaults();

   private:
    std::unordered_map<monitor::DcmiMetricKind, Factory> factories_;
    std::unordered_map<monitor::DcmiMetricKind, monitor::DcmiMetricMeta> meta_;
};

}  // namespace dcmi
}  // namespace ipc_monitor
}  // namespace dynolog_npu

#endif  // MSMONITOR_DCMI_METRIC_REGISTRY_H
