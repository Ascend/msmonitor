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
