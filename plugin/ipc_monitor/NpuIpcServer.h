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
#ifndef NPU_IPC_SERVER_H
#define NPU_IPC_SERVER_H

#include <string>

#include "NpuIpcClient.h"

namespace dynolog_npu
{
namespace ipc_monitor
{

// IPC 服务端：与 PTA 侧 IpcClient 的收发方向严格对齐。
//   bind   $HOME/<base>[_<uid>].sock   路径名 SOCK_DGRAM，由本端创建
//   收包   PTA 发往上述路径名，本端按 Metadata 两段式读取
//   回包   发往 PTA 绑定的抽象地址 @<src>
class NpuIpcServer
{
   public:
    explicit NpuIpcServer(const std::string &baseName = DYNO_IPC_NAME);
    ~NpuIpcServer() = default;

    NpuIpcServer(const NpuIpcServer &) = delete;
    NpuIpcServer &operator=(const NpuIpcServer &) = delete;

    // 非阻塞取一条消息，成功时填充 type/payload/src
    bool PollOnce(std::string &type, std::string &payload, std::string &src);

    // 以 type 把 payload 回给 src（抽象地址）
    bool Reply(const std::string &src, const std::string &type, const std::string &payload);

    const std::string &Name() const { return name_; }

   private:
    static std::string MakeName(const std::string &baseName);

    std::string name_;
    NpuIpcEndPoint<0> ep_;
};

}  // namespace ipc_monitor
}  // namespace dynolog_npu

#endif  // NPU_IPC_SERVER_H
