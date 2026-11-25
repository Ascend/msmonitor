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
#include <gtest/gtest.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <vector>

#include "NpuIpcClient.h"
#include "NpuIpcEndPoint.h"
#include "NpuIpcServer.h"

using namespace dynolog_npu::ipc_monitor;  // NOLINT

namespace
{
std::string UniqueName(const std::string &tag) { return tag + "_" + std::to_string(getpid()); }

// 模拟 PTA 侧：同步发送一条消息到指定的路径名 socket
bool SendTo(const std::string &dest, NpuIpcEndPoint<0> &ep, const std::string &type, const std::string &payload)
{
    Metadata metadata;
    metadata.size = payload.size();
    if (memcpy_s(metadata.type, sizeof(metadata.type), type.c_str(), type.size() + 1) != EOK)
    {
        return false;
    }
    std::vector<NpuPayLoad> loads{NpuPayLoad(sizeof(struct Metadata), &metadata),
                                  NpuPayLoad(payload.size(), const_cast<char *>(payload.data()))};
    try
    {
        auto ctxt = ep.BuildSendNpuCtxt(dest, loads, std::vector<int>());
        return ep.TrySendMessage(*ctxt);
    }
    catch (const std::exception &)
    {
        return false;
    }
}

// 模拟 PTA 侧：从自己的抽象地址收一条回包
bool RecvFrom(NpuIpcEndPoint<0> &ep, std::string &type, std::string &payload)
{
    Metadata metadata;
    std::vector<NpuPayLoad> peekLoads{NpuPayLoad(sizeof(struct Metadata), &metadata)};
    auto peekCtxt = ep.BuildNpuRcvCtxt(peekLoads);
    if (!ep.TryPeekMessage(*peekCtxt))
    {
        return false;
    }
    std::vector<char> buf(metadata.size);
    std::vector<NpuPayLoad> loads{NpuPayLoad(sizeof(struct Metadata), &metadata),
                                  NpuPayLoad(metadata.size, buf.data())};
    auto ctxt = ep.BuildNpuRcvCtxt(loads);
    if (!ep.TryRcvMessage(*ctxt))
    {
        return false;
    }
    type.assign(metadata.type, strnlen(metadata.type, sizeof(metadata.type)));
    payload.assign(buf.begin(), buf.end());
    return true;
}
}  // namespace

TEST(NpuIpcServerTest, TestNameShouldKeepBaseNamePrefixWhenConstructed)
{
    NpuIpcServer server(UniqueName("ut_dynolog_name"));
    EXPECT_EQ(server.Name().compare(0, 14, "ut_dynolog_nam"), 0);
}

TEST(NpuIpcServerTest, TestPollShouldReturnFalseWhenNoMessage)
{
    NpuIpcServer server(UniqueName("ut_dynolog_empty"));
    std::string type;
    std::string payload;
    std::string src;
    EXPECT_FALSE(server.PollOnce(type, payload, src));
}

TEST(NpuIpcServerTest, TestReplyShouldBeReceivedByClientWhenRequestPolled)
{
    NpuIpcServer server(UniqueName("ut_dynolog_rt"));
    const std::string clientName = UniqueName("utclient_rt");
    NpuIpcEndPoint<0> client(clientName);

    ASSERT_TRUE(SendTo(server.Name(), client, MSG_TYPE_REQUEST, "PING"));

    std::string type;
    std::string payload;
    std::string src;
    ASSERT_TRUE(server.PollOnce(type, payload, src));
    EXPECT_EQ(type, MSG_TYPE_REQUEST);
    EXPECT_EQ(payload, "PING");
    // 抽象地址没有结尾 '\0'，长度必须按 msg_namelen 截断，不能带出脏字节
    EXPECT_EQ(src, clientName);

    ASSERT_TRUE(server.Reply(src, type, "PONG"));

    std::string respType;
    std::string respPayload;
    ASSERT_TRUE(RecvFrom(client, respType, respPayload));
    EXPECT_EQ(respPayload, "PONG");
}

TEST(NpuIpcServerTest, TestPollShouldDrainAllMessagesWhenMultipleQueued)
{
    NpuIpcServer server(UniqueName("ut_dynolog_queue"));
    NpuIpcEndPoint<0> client(UniqueName("utclient_queue"));

    ASSERT_TRUE(SendTo(server.Name(), client, MSG_TYPE_REQUEST, "one"));
    ASSERT_TRUE(SendTo(server.Name(), client, MSG_TYPE_REQUEST, "two"));

    std::string type;
    std::string payload;
    std::string src;
    ASSERT_TRUE(server.PollOnce(type, payload, src));
    EXPECT_EQ(payload, "one");
    ASSERT_TRUE(server.PollOnce(type, payload, src));
    EXPECT_EQ(payload, "two");
}

TEST(NpuIpcServerTest, TestPollShouldDropWholeMessageWhenOversized)
{
    NpuIpcServer server(UniqueName("ut_dynolog_big"));
    NpuIpcEndPoint<0> client(UniqueName("utclient_big"));

    std::string oversized(MAX_IPC_MESSAGE_SIZE + 100, 'x');
    ASSERT_TRUE(SendTo(server.Name(), client, MSG_TYPE_REQUEST, oversized));

    std::string type;
    std::string payload;
    std::string src;
    EXPECT_FALSE(server.PollOnce(type, payload, src));

    // 超长消息必须整包收走：MSG_PEEK 不消费报文，不排空的话下一轮还会看到同一帧，后续消息永远取不到
    ASSERT_TRUE(SendTo(server.Name(), client, MSG_TYPE_REQUEST, "after"));
    ASSERT_TRUE(server.PollOnce(type, payload, src));
    EXPECT_EQ(payload, "after");
}

TEST(NpuIpcServerTest, TestReplyShouldReturnFalseWhenArgumentEmpty)
{
    NpuIpcServer server(UniqueName("ut_dynolog_bad"));
    EXPECT_FALSE(server.Reply("", MSG_TYPE_REQUEST, "payload"));
    EXPECT_FALSE(server.Reply("somewhere", "", "payload"));
    EXPECT_FALSE(server.Reply("somewhere", MSG_TYPE_REQUEST, ""));
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
