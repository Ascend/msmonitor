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
#include "MetricKernelProcess.h"

#include <numeric>

namespace dynolog_npu
{
namespace ipc_monitor
{
namespace metric
{

std::string KernelMetric::seriesToJson() const
{
    nlohmann::json jsonMsg;
    jsonMsg["kind"] = "Kernel";
    jsonMsg["deviceId"] = deviceId;
    jsonMsg["duration"] = duration;
    jsonMsg["timestamp"] = timestamp;
    return jsonMsg.dump();
}

void MetricKernelProcess::ConsumeMsptiData(msptiActivity* record)
{
    msptiActivityKernel* kernel = ReinterpretConvert<msptiActivityKernel*>(record);
    std::shared_ptr<msptiActivityKernel> tmp;
    MakeSharedPtr(tmp);
    if (tmp == nullptr || memcpy_s(tmp.get(), sizeof(msptiActivityKernel), kernel, sizeof(msptiActivityKernel)) != EOK)
    {
        LOG(ERROR) << "memcpy_s failed " << IPC_ERROR(ErrCode::MEMORY);
        return;
    }
    {
        std::unique_lock<std::mutex> lock(dataMutex);
        records.emplace_back(std::move(tmp));
    }
}

std::vector<KernelMetric> MetricKernelProcess::AggregatedData()
{
    std::vector<std::shared_ptr<msptiActivityKernel>> copyRecords;
    {
        std::unique_lock<std::mutex> lock(dataMutex);
        copyRecords = std::move(records);
        records.clear();
    }
    if (copyRecords.empty())
    {
        return {};
    }
    std::unordered_map<uint32_t, std::vector<std::shared_ptr<msptiActivityKernel>>> deviceId2KernelData =
        groupby(copyRecords,
                [](const std::shared_ptr<msptiActivityKernel>& data) -> std::uint32_t { return data->ds.deviceId; });
    std::vector<KernelMetric> ans;
    auto curTimestamp = getCurrentTimestamp64();
    for (auto& pair : deviceId2KernelData)
    {
        auto deviceId = pair.first;
        auto& kernelDatas = pair.second;
        KernelMetric kernelMetric{};
        kernelMetric.duration = std::accumulate(kernelDatas.begin(), kernelDatas.end(), 0ULL,
                                                [](uint64_t acc, std::shared_ptr<msptiActivityKernel> kernel)
                                                { return acc + kernel->end - kernel->start; });
        kernelMetric.deviceId = deviceId;
        kernelMetric.timestamp = curTimestamp;
        ans.emplace_back(kernelMetric);
    }

    return ans;
}

void MetricKernelProcess::SendProcessMessage()
{
    auto afterAggregated = AggregatedData();
    for (auto& metric : afterAggregated)
    {
        SendMessage(metric.seriesToJson());
    }
}

void MetricKernelProcess::Clear() { records.clear(); }
}  // namespace metric
}  // namespace ipc_monitor
}  // namespace dynolog_npu
