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
#include "NpuIpcServer.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace dynolog_npu
{
namespace ipc_monitor
{

std::string NpuIpcServer::MakeName(const std::string &baseName)
{
    // 必须与 IpcClient::GetDynoIpcName() 同一规则，否则 PTA 侧找不到 socket 文件
    auto hostUid = GetHostUid();
    return hostUid.empty() ? baseName : baseName + "_" + hostUid;
}

NpuIpcServer::NpuIpcServer(const std::string &baseName) : name_(MakeName(baseName)), ep_(MakeName(baseName), false) {}

bool NpuIpcServer::PollOnce(std::string &type, std::string &payload, std::string &src)
{
    try
    {
        Metadata metadata;
        std::vector<NpuPayLoad> peekLoad{NpuPayLoad(sizeof(struct Metadata), &metadata)};
        auto peekCtxt = ep_.BuildNpuRcvCtxt(peekLoad);
        if (!ep_.TryPeekMessage(*peekCtxt))
        {
            return false;
        }
        if (metadata.size > MAX_IPC_MESSAGE_SIZE)
        {
            // MSG_PEEK 不消费报文，这里只探测头部长度。超限时必须把这条报文
            // 收走，否则下一轮 MSG_PEEK 还会看到同一帧，服务端卡在坏消息上。
            // 缓冲区固定用上限值：不能按 metadata.size 分配，也不能循环接收——
            // SOCK_DGRAM 一次 recv 即消费整条报文，装不下的尾部由内核截断丢弃。
            std::vector<char> drain(MAX_IPC_MESSAGE_SIZE);
            std::vector<NpuPayLoad> drainLoad{NpuPayLoad(drain.size(), drain.data())};
            auto drainCtxt = ep_.BuildNpuRcvCtxt(drainLoad);
            ep_.TryRcvMessage(*drainCtxt);
            return false;
        }

        // PTA 侧 bind 的是抽象地址，src 从 sun_path + 1 取。
        // GetName 遇到非抽象来源会抛异常，这里必须先消费掉这条消息再返回，
        // 否则 MSG_PEEK 会一直看到同一帧，服务端会卡死在同一条消息上。
        const char *srcName = nullptr;
        size_t srcLen = 0;
        try
        {
            srcName = ep_.GetName(*peekCtxt, true);
            if (srcName != nullptr)
            {
                // 抽象地址没有结尾 '\0'，必须按内核回填的 msg_namelen 截断；
                // 否则 std::string 会继续读 sun_path 的未初始化区域，拿到脏地址。
                const size_t prefix = sizeof(sa_family_t) + 1;
                if (peekCtxt->msghdr.msg_namelen > prefix)
                {
                    srcLen = peekCtxt->msghdr.msg_namelen - prefix;
                    // 对端 SetSocketAdress 的 addrlen 含结尾 '\0'，内核原样回填；
                    // 这里必须去掉，否则 src 与客户端名字不相等，回包地址也会多一字节。
                    if (srcLen > 0 && srcName[srcLen - 1] == '\0')
                    {
                        --srcLen;
                    }
                }
                else
                {
                    srcLen = std::strlen(srcName);
                }
            }
        }
        catch (const std::exception &)
        {
            srcName = nullptr;
        }
        if (srcName == nullptr)
        {
            (void)ep_.TryRcvMessage(*peekCtxt);
            return false;
        }
        src.assign(srcName, srcLen);

        std::vector<unsigned char> buf(metadata.size);
        std::vector<NpuPayLoad> recvLoad{NpuPayLoad(sizeof(struct Metadata), &metadata),
                                         NpuPayLoad(metadata.size, buf.data())};
        auto recvCtxt = ep_.BuildNpuRcvCtxt(recvLoad);
        if (!ep_.TryRcvMessage(*recvCtxt))
        {
            return false;
        }

        type = std::string(metadata.type, strnlen(metadata.type, TYPE_SIZE));
        payload.clear();
        if (!buf.empty())
        {
            payload.assign(ReinterpretConvert<const char *>(buf.data()), buf.size());
        }
        return true;
    }
    catch (const std::exception &)
    {
        return false;
    }
}

bool NpuIpcServer::Reply(const std::string &src, const std::string &type, const std::string &payload)
{
    if (src.empty() || type.empty() || type.size() + 1 > TYPE_SIZE || payload.size() > MAX_IPC_MESSAGE_SIZE)
    {
        return false;
    }
    Metadata metadata;
    if (memset_s(&metadata, sizeof(metadata), 0, sizeof(metadata)) != EOK)
    {
        return false;
    }
    metadata.size = payload.size();
    if (memcpy_s(metadata.type, sizeof(metadata.type), type.c_str(), type.size() + 1) != EOK)
    {
        return false;
    }

    std::vector<NpuPayLoad> loads{NpuPayLoad(sizeof(struct Metadata), &metadata),
                                  NpuPayLoad(payload.size(), const_cast<char *>(payload.data()))};
    try
    {
        auto ctxt = ep_.BuildSendNpuCtxt(src, loads, std::vector<int>(), true);
        return ep_.TrySendMessage(*ctxt);
    }
    catch (const std::exception &)
    {
        return false;
    }
}

}  // namespace ipc_monitor
}  // namespace dynolog_npu
