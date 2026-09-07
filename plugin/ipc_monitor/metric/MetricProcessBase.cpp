/*
 * Copyright (C) 2025-2026. Huawei Technologies Co., Ltd. All rights reserved.
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

#include "MetricProcessBase.h"

#include "NpuIpcClient.h"
#include "utils.h"

namespace dynolog_npu
{
namespace ipc_monitor
{
namespace metric
{

void MetricProcessBase::SendMessage(std::string message)
{
    if (message.empty())
    {
        LOG(ERROR) << "SendMessage message is empty";
        return;
    }
    static const std::string destName = IpcClient::GetDynoIpcName("_data");
    static const int maxRetry = 5;
    static const int retryWaitTimeUs = 1000;
    auto msg = Message::ConstructStrMessage(message, MSG_TYPE_DATA);
    if (!msg)
    {
        LOG(ERROR) << "ConstructStrMessage failed, message: " << message;
        return;
    }
    auto ipcClient = DynoLogNpuMonitor::GetInstance()->GetIpcClient();
    if (!ipcClient)
    {
        LOG(ERROR) << "DynoLogNpuMonitor ipcClient is nullptr";
        return;
    }
    if (!ipcClient->SyncSendMessage(*msg, destName, maxRetry, retryWaitTimeUs))
    {
        LOG(ERROR) << "send mspti message failed: " << message;
    }
}

}  // namespace metric
}  // namespace ipc_monitor
}  // namespace dynolog_npu
