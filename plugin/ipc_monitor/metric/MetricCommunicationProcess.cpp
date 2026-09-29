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
#include "MetricCommunicationProcess.h"

#include <nlohmann/json.hpp>
#include <numeric>

#include "utils.h"

namespace dynolog_npu
{
namespace ipc_monitor
{
namespace metric
{

std::string CommunicationMetric::seriesToJson() const
{
    nlohmann::json jsonMsg;
    jsonMsg["kind"] = "Communication";
    jsonMsg["deviceId"] = deviceId;
    jsonMsg["duration"] = duration;
    jsonMsg["timestamp"] = timestamp;
    return jsonMsg.dump();
}

void MetricCommunicationProcess::ConsumeMsptiData(msptiActivity* record)
{
    msptiActivityCommunication* communicationData = ReinterpretConvert<msptiActivityCommunication*>(record);
    std::shared_ptr<msptiActivityCommunication> tmp;
    MakeSharedPtr(tmp);
    if (tmp == nullptr || memcpy_s(tmp.get(), sizeof(msptiActivityCommunication), communicationData,
                                   sizeof(msptiActivityCommunication)) != EOK)
    {
        LOG(ERROR) << "memcpy_s failed " << IPC_ERROR(ErrCode::MEMORY);
        return;
    }
    {
        std::unique_lock<std::mutex> lock(dataMutex);
        records.emplace_back(std::move(tmp));
    }
}

std::vector<CommunicationMetric> MetricCommunicationProcess::AggregatedData()
{
    std::vector<std::shared_ptr<msptiActivityCommunication>> copyRecords;
    {
        std::unique_lock<std::mutex> lock(dataMutex);
        copyRecords = std::move(records);
        records.clear();
    }
    if (copyRecords.empty())
    {
        return {};
    }
    std::unordered_map<uint32_t, std::vector<std::shared_ptr<msptiActivityCommunication>>> deviceId2CommunicationData =
        groupby(copyRecords, [](const std::shared_ptr<msptiActivityCommunication>& data) -> std::uint32_t
                { return data->ds.deviceId; });
    std::vector<CommunicationMetric> ans;
    auto curTimestamp = getCurrentTimestamp64();
    for (auto& pair : deviceId2CommunicationData)
    {
        CommunicationMetric communicationMetric{};
        auto& communicationDatas = pair.second;
        communicationMetric.duration =
            std::accumulate(communicationDatas.begin(), communicationDatas.end(), 0ULL,
                            [](uint64_t acc, std::shared_ptr<msptiActivityCommunication> communication)
                            { return acc + communication->end - communication->start; });
        communicationMetric.deviceId = pair.first;
        communicationMetric.timestamp = curTimestamp;
        ans.emplace_back(communicationMetric);
    }
    return ans;
}

void MetricCommunicationProcess::SendProcessMessage()
{
    auto afterAggregated = AggregatedData();
    for (auto& metric : afterAggregated)
    {
        SendMessage(metric.seriesToJson());
    }
}

void MetricCommunicationProcess::Clear() { records.clear(); }
}  // namespace metric
}  // namespace ipc_monitor
}  // namespace dynolog_npu
