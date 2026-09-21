/*
 * Copyright (C) 2025-2025. Huawei Technologies Co., Ltd. All rights reserved.
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
#include "MetricManager.h"

#include "MetricApiProcess.h"
#include "MetricCommunicationProcess.h"
#include "MetricHcclProcess.h"
#include "MetricKernelProcess.h"
#include "MetricMarkProcess.h"
#include "MetricMemCpyProcess.h"
#include "MetricMemProcess.h"
#include "MetricMemSetProcess.h"
#include "utils.h"

namespace dynolog_npu
{
namespace ipc_monitor
{
namespace metric
{

MetricManager::MetricManager()
    : MsptiDataProcessBase("MetricManager"),
      kindSwitches_(MSPTI_ACTIVITY_KIND_COUNT),
      consumeStatus_(MSPTI_ACTIVITY_KIND_COUNT)
{
    metrics.resize(MSPTI_ACTIVITY_KIND_COUNT);
    metrics[MSPTI_ACTIVITY_KIND_KERNEL] = std::make_shared<MetricKernelProcess>();
    metrics[MSPTI_ACTIVITY_KIND_API] = std::make_shared<MetricApiProcess>("API");
    metrics[MSPTI_ACTIVITY_KIND_MEMCPY] = std::make_shared<MetricMemCpyProcess>();
    metrics[MSPTI_ACTIVITY_KIND_MARKER] = std::make_shared<MetricMarkProcess>();
    metrics[MSPTI_ACTIVITY_KIND_MEMSET] = std::make_shared<MetricMemSetProcess>();
    metrics[MSPTI_ACTIVITY_KIND_HCCL] = std::make_shared<MetricHcclProcess>();
    metrics[MSPTI_ACTIVITY_KIND_MEMORY] = std::make_shared<MetricMemProcess>();
    metrics[MSPTI_ACTIVITY_KIND_COMMUNICATION] = std::make_shared<MetricCommunicationProcess>();
    metrics[MSPTI_ACTIVITY_KIND_ACL_API] = std::make_shared<MetricApiProcess>("AclAPI");
    metrics[MSPTI_ACTIVITY_KIND_NODE_API] = std::make_shared<MetricApiProcess>("NodeAPI");
    metrics[MSPTI_ACTIVITY_KIND_RUNTIME_API] = std::make_shared<MetricApiProcess>("RuntimeAPI");
}

void MetricManager::RunPostTask()
{
    for (int i = 0; i < MSPTI_ACTIVITY_KIND_COUNT; i++)
    {
        if (kindSwitches_[i].load())
        {
            kindSwitches_[i] = false;
            if (metrics[i] == nullptr)
            {
                LOG(ERROR) << "Metric processor is null, kind: " << i;
                continue;
            }
            metrics[i]->Clear();
        }
    }
}

ErrCode MetricManager::ConsumeMsptiData(msptiActivity *record)
{
    if (record->kind <= MSPTI_ACTIVITY_KIND_INVALID || record->kind >= MSPTI_ACTIVITY_KIND_COUNT)
    {
        return ErrCode::PARAM;
    }
    if (!kindSwitches_[record->kind])
    {
        return ErrCode::PERMISSION;
    }
    auto metricProcess = metrics[record->kind];
    if (metricProcess == nullptr)
    {
        LOG(ERROR) << "Metric processor is null, kind: " << static_cast<int32_t>(record->kind);
        return ErrCode::NOT_SUPPORT;
    }
    consumeStatus_[record->kind] = true;
    metricProcess->ConsumeMsptiData(record);
    consumeStatus_[record->kind] = false;
    return ErrCode::SUC;
}

void MetricManager::SetReportInterval(uint32_t intervalTimes)
{
    if (reportInterval_.load() != intervalTimes)
    {
        SendMetricMsg();
        SetInterval(intervalTimes);
        reportInterval_.store(intervalTimes);
    }
}

void MetricManager::ExecuteTask() { SendMetricMsg(); }

void MetricManager::SendMetricMsg()
{
    for (int i = 0; i < MSPTI_ACTIVITY_KIND_COUNT; i++)
    {
        if (kindSwitches_[i].load())
        {
            if (metrics[i] == nullptr)
            {
                LOG(ERROR) << "Metric processor is null, kind: " << i;
                continue;
            }
            metrics[i]->SendProcessMessage();
        }
    }
}

void MetricManager::EnableKindSwitch(msptiActivityKind kind, bool flag) { kindSwitches_[kind] = flag; }
}  // namespace metric
}  // namespace ipc_monitor
}  // namespace dynolog_npu
