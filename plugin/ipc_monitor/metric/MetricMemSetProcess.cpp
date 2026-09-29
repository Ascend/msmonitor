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
#include "MetricMemSetProcess.h"

#include <numeric>

namespace dynolog_npu
{
namespace ipc_monitor
{
namespace metric
{

std::string MemSetMetric::seriesToJson() const
{
    nlohmann::json jsonMsg;
    jsonMsg["kind"] = "MemSet";
    jsonMsg["deviceId"] = deviceId;
    jsonMsg["duration"] = duration;
    jsonMsg["timestamp"] = timestamp;
    return jsonMsg.dump();
}

void MetricMemSetProcess::ConsumeMsptiData(msptiActivity* record)
{
    msptiActivityMemset* memSet = ReinterpretConvert<msptiActivityMemset*>(record);
    std::shared_ptr<msptiActivityMemset> tmp;
    MakeSharedPtr(tmp);
    if (tmp == nullptr || memcpy_s(tmp.get(), sizeof(msptiActivityMemset), memSet, sizeof(msptiActivityMemset)) != EOK)
    {
        LOG(ERROR) << "memcpy_s failed " << IPC_ERROR(ErrCode::MEMORY);
        return;
    }
    {
        std::unique_lock<std::mutex> lock(dataMutex);
        records.emplace_back(std::move(tmp));
    }
}

std::vector<MemSetMetric> MetricMemSetProcess::AggregatedData()
{
    std::vector<std::shared_ptr<msptiActivityMemset>> copyRecords;
    {
        std::unique_lock<std::mutex> lock(dataMutex);
        copyRecords = std::move(records);
        records.clear();
    }
    if (copyRecords.empty())
    {
        return {};
    }
    std::unordered_map<uint32_t, std::vector<std::shared_ptr<msptiActivityMemset>>> deviceId2MemsetData = groupby(
        copyRecords, [](const std::shared_ptr<msptiActivityMemset>& data) -> std::uint32_t { return data->deviceId; });
    std::vector<MemSetMetric> ans;
    auto curTimestamp = getCurrentTimestamp64();
    for (auto& pair : deviceId2MemsetData)
    {
        MemSetMetric memSetMetric{};
        auto deviceId = pair.first;
        auto& memSetDatas = pair.second;
        memSetMetric.duration = std::accumulate(memSetDatas.begin(), memSetDatas.end(), 0ULL,
                                                [](uint64_t acc, std::shared_ptr<msptiActivityMemset> memSet)
                                                { return acc + memSet->end - memSet->start; });
        memSetMetric.deviceId = deviceId;
        memSetMetric.timestamp = curTimestamp;
        ans.emplace_back(memSetMetric);
    }
    return ans;
}

void MetricMemSetProcess::SendProcessMessage()
{
    auto afterAggregated = AggregatedData();
    for (auto& metric : afterAggregated)
    {
        SendMessage(metric.seriesToJson());
    }
}

void MetricMemSetProcess::Clear() { records.clear(); }
}  // namespace metric
}  // namespace ipc_monitor
}  // namespace dynolog_npu
