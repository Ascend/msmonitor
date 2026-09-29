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
#include "MetricApiProcess.h"

#include <nlohmann/json.hpp>
#include <numeric>

#include "utils.h"

namespace dynolog_npu
{
namespace ipc_monitor
{
namespace metric
{

std::string ApiMetric::seriesToJson() const
{
    nlohmann::json jsonMsg;
    jsonMsg["kind"] = kind;
    jsonMsg["deviceId"] = -1;
    jsonMsg["duration"] = duration;
    jsonMsg["timestamp"] = timestamp;
    return jsonMsg.dump();
}

void MetricApiProcess::ConsumeMsptiData(msptiActivity* record)
{
    msptiActivityApi* apiData = ReinterpretConvert<msptiActivityApi*>(record);
    std::shared_ptr<msptiActivityApi> tmp;
    MakeSharedPtr(tmp);
    if (tmp == nullptr || memcpy_s(tmp.get(), sizeof(msptiActivityApi), apiData, sizeof(msptiActivityApi)) != EOK)
    {
        LOG(ERROR) << "memcpy_s failed " << IPC_ERROR(ErrCode::MEMORY);
        return;
    }
    {
        std::unique_lock<std::mutex> lock(dataMutex);
        records.emplace_back(std::move(tmp));
    }
}

std::vector<ApiMetric> MetricApiProcess::AggregatedData()
{
    std::vector<std::shared_ptr<msptiActivityApi>> copyRecords;
    {
        std::unique_lock<std::mutex> lock(dataMutex);
        copyRecords = std::move(records);
        records.clear();
    }
    if (copyRecords.empty())
    {
        return {};
    }
    ApiMetric apiMetric{};
    auto ans = std::accumulate(copyRecords.begin(), copyRecords.end(), 0ULL,
                               [](uint64_t acc, std::shared_ptr<msptiActivityApi> api)
                               { return acc + api->end - api->start; });
    apiMetric.duration = ans;
    apiMetric.deviceId = -1;
    apiMetric.timestamp = getCurrentTimestamp64();
    apiMetric.kind = apiKind;
    return {apiMetric};
}

void MetricApiProcess::SendProcessMessage()
{
    auto afterAggregated = AggregatedData();
    for (auto& metric : afterAggregated)
    {
        SendMessage(metric.seriesToJson());
    }
}

void MetricApiProcess::Clear() { records.clear(); }
}  // namespace metric
}  // namespace ipc_monitor
}  // namespace dynolog_npu
