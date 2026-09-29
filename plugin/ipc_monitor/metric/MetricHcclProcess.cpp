/*
 * -------------------------------------------------------------------------
 * This file is part of the MindStudio project.
 * Copyright (c) 2026 Huawei Technologies Co.,Ltd.
 *
 * MindStudio is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *
 *          http://license.coscl.org.cn/MulanPSL2
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * -------------------------------------------------------------------------
 */
#include "MetricHcclProcess.h"

#include <nlohmann/json.hpp>
#include <numeric>

#include "utils.h"

namespace dynolog_npu
{
namespace ipc_monitor
{
namespace metric
{

std::string HcclMetric::seriesToJson() const
{
    nlohmann::json jsonMsg;
    jsonMsg["kind"] = "Hccl";
    jsonMsg["deviceId"] = deviceId;
    jsonMsg["duration"] = duration;
    jsonMsg["timestamp"] = timestamp;
    return jsonMsg.dump();
}

void MetricHcclProcess::ConsumeMsptiData(msptiActivity* record)
{
    msptiActivityHccl* hcclData = ReinterpretConvert<msptiActivityHccl*>(record);
    std::shared_ptr<msptiActivityHccl> tmp;
    MakeSharedPtr(tmp);
    if (tmp == nullptr || memcpy_s(tmp.get(), sizeof(msptiActivityHccl), hcclData, sizeof(msptiActivityHccl)) != EOK)
    {
        LOG(ERROR) << "memcpy_s failed " << IPC_ERROR(ErrCode::MEMORY);
        return;
    }
    {
        std::unique_lock<std::mutex> lock(dataMutex);
        records.emplace_back(std::move(tmp));
    }
}

std::vector<HcclMetric> MetricHcclProcess::AggregatedData()
{
    std::vector<std::shared_ptr<msptiActivityHccl>> copyRecords;
    {
        std::unique_lock<std::mutex> lock(dataMutex);
        copyRecords = std::move(records);
        records.clear();
    }
    if (copyRecords.empty())
    {
        return {};
    }
    std::unordered_map<uint32_t, std::vector<std::shared_ptr<msptiActivityHccl>>> deviceId2HcclData = groupby(
        copyRecords, [](const std::shared_ptr<msptiActivityHccl>& data) -> std::uint32_t { return data->ds.deviceId; });
    std::vector<HcclMetric> ans;
    auto curTimestamp = getCurrentTimestamp64();
    for (auto& pair : deviceId2HcclData)
    {
        HcclMetric hcclMetric{};
        auto& hcclData = pair.second;
        hcclMetric.duration = std::accumulate(hcclData.begin(), hcclData.end(), 0ULL,
                                              [](uint64_t acc, std::shared_ptr<msptiActivityHccl> hccl)
                                              { return acc + hccl->end - hccl->start; });
        hcclMetric.deviceId = pair.first;
        hcclMetric.timestamp = curTimestamp;
        ans.emplace_back(hcclMetric);
    }
    return ans;
}

void MetricHcclProcess::SendProcessMessage()
{
    auto afterAggregated = AggregatedData();
    for (auto& metric : afterAggregated)
    {
        SendMessage(metric.seriesToJson());
    }
}

void MetricHcclProcess::Clear() { records.clear(); }
}  // namespace metric
}  // namespace ipc_monitor
}  // namespace dynolog_npu
