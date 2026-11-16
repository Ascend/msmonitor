/*
 * Copyright (C) 2025-2026. Huawei Technologies Co., Ltd. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 */

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <mockcpp/mockcpp.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "plugin/ipc_monitor/NpuIpcClient.h"
#include "plugin/ipc_monitor/utils/utils.h"

using namespace dynolog_npu::ipc_monitor;

namespace
{
bool PeekOversizedMessage(NpuIpcEndPoint<0> *, NpuIpcEndPointCtxt<0> &ctxt)
{
    auto *metadata = static_cast<Metadata *>(ctxt.iov[0].iov_base);
    metadata->size = MAX_IPC_MESSAGE_SIZE + 1;
    return true;
}
}  // namespace

class NpuIpcClientTest : public ::testing::Test
{
   protected:
    void SetUp() override { GlobalMockObject::verify(); }
    void TearDown() override { GlobalMockObject::verify(); }

    std::string capturedDest_;
};

// 1. utils: CalcHashId deterministic
TEST_F(NpuIpcClientTest, CalcHashId_Deterministic)
{
    EXPECT_EQ(CalcHashId("test"), CalcHashId("test"));
    EXPECT_NE(CalcHashId("hostA"), CalcHashId("hostB"));
    EXPECT_NE(CalcHashId(""), CalcHashId("a"));
}

// 2. utils: GetHostName / GetHostUid not throw and returns string
TEST_F(NpuIpcClientTest, GetHostNameAndUid_Basic)
{
    EXPECT_NO_THROW(GetHostName());
    EXPECT_NO_THROW(GetHostUid());
    // GetHostName may be empty in isolated env, but string type is valid
    std::string hn = GetHostName();
    std::string uid = GetHostUid();
    // uid is either empty or numeric string (hash)
    if (!uid.empty())
    {
        for (char c : uid) EXPECT_TRUE(isdigit(c) || c == '-');
    }
    (void)hn;
}

// 3. IpcClient::GetDynoIpcName
TEST_F(NpuIpcClientTest, GetDynoIpcName_HostUidPresent)
{
    MOCKER_CPP(&GetHostUid).stubs().will(returnValue(std::string("123456")));
    EXPECT_EQ(IpcClient::GetDynoIpcName(), DYNO_IPC_NAME + std::string("_123456"));
    GlobalMockObject::verify();
}

TEST_F(NpuIpcClientTest, GetDynoIpcName_HostUidEmpty_ReturnBase)
{
    MOCKER_CPP(&GetHostUid).stubs().will(returnValue(std::string("")));
    EXPECT_EQ(IpcClient::GetDynoIpcName(), DYNO_IPC_NAME);
    GlobalMockObject::verify();
}

TEST_F(NpuIpcClientTest, GetDynoIpcName_HostUidEmpty_NoHostnameFallback)
{
    // abandoned hostname fallback: empty uid must return base, not hostname hash
    MOCKER_CPP(&GetHostUid).stubs().will(returnValue(std::string("")));
    EXPECT_EQ(IpcClient::GetDynoIpcName(), DYNO_IPC_NAME);
    // ensure implementation does not implicitly use hostname
    EXPECT_NE(IpcClient::GetDynoIpcName(), DYNO_IPC_NAME + "_" + std::to_string(CalcHashId("anyhost")));
    GlobalMockObject::verify();
}

// 4. suffix 形参：MetricProcessBase 经 GetDynoIpcName("_data") 复用本接口
TEST_F(NpuIpcClientTest, GetDynoIpcName_Suffix_HostUidPresent)
{
    MOCKER_CPP(&GetHostUid).stubs().will(returnValue(std::string("88888")));
    EXPECT_EQ(IpcClient::GetDynoIpcName("_data"), DYNO_IPC_NAME + std::string("_data_88888"));
    GlobalMockObject::verify();
}

TEST_F(NpuIpcClientTest, GetDynoIpcName_Suffix_HostUidEmpty)
{
    MOCKER_CPP(&GetHostUid).stubs().will(returnValue(std::string("")));
    EXPECT_EQ(IpcClient::GetDynoIpcName("_data"), DYNO_IPC_NAME + std::string("_data"));
    GlobalMockObject::verify();
}

TEST_F(NpuIpcClientTest, GetDynoIpcName_DefaultSuffixIsEmpty)
{
    MOCKER_CPP(&GetHostUid).stubs().will(returnValue(std::string("777")));
    EXPECT_EQ(IpcClient::GetDynoIpcName(), IpcClient::GetDynoIpcName(""));
    GlobalMockObject::verify();
}

TEST_F(NpuIpcClientTest, GetDynoIpcName_SuffixBeforeHostUid)
{
    // Keep the socket purpose before the host UID: dynolog_data_<host_uid>.
    MOCKER_CPP(&GetHostUid).stubs().will(returnValue(std::string("XYZ")));
    EXPECT_EQ(IpcClient::GetDynoIpcName("_data"), DYNO_IPC_NAME + std::string("_data_XYZ"));
    GlobalMockObject::verify();
}

// 5. 未 mock 的真实路径：非空且以 DYNO_IPC_NAME 为前缀
TEST_F(NpuIpcClientTest, GetDynoIpcName_Actual_ReturnsWithPrefix)
{
    std::string name = IpcClient::GetDynoIpcName();
    EXPECT_FALSE(name.empty());
    EXPECT_EQ(name.compare(0, DYNO_IPC_NAME.size(), DYNO_IPC_NAME), 0);
}

// 6. IpcClient lifecycle（ipcName_ 在构造时解析，Init() 只刷新 pids_）
TEST_F(NpuIpcClientTest, Init_DoesNotThrow)
{
    IpcClient client;
    EXPECT_NO_THROW(client.Init());
    EXPECT_TRUE(client.Init());
}

TEST_F(NpuIpcClientTest, SyncSendMessage_EmptyDest_ReturnsFalse)
{
    IpcClient client;
    Message msg;
    msg.metadata.size = 0;
    msg.buf = nullptr;
    // TYPE_SIZE zeroed already
    EXPECT_FALSE(client.SyncSendMessage(msg, ""));
    EXPECT_FALSE(client.SyncSendMessage(msg, "", 10, 10000));
}

TEST_F(NpuIpcClientTest, RegisterInstance_NoServer_ReturnsFalseOrNoThrow)
{
    IpcClient client;
    client.Init();
    // Without dynolog server, RegisterInstance should return false but not throw
    EXPECT_NO_THROW({
        bool ret = client.RegisterInstance(0);
        // ret may be false (no server) ; just ensure bool
        (void)ret;
    });
}

TEST_F(NpuIpcClientTest, SendNpuStatus_NoServer_NoThrow)
{
    IpcClient client;
    client.Init();
    NpuStatus status;
    status.status = 0;
    status.currentStep = 1;
    status.pid = GetProcessId();
    EXPECT_NO_THROW({
        bool ret = client.SendNpuStatus(status, MSG_TYPE_MONITOR_STATUS);
        (void)ret;
    });
}

TEST_F(NpuIpcClientTest, IpcClientNpuConfig_NoServer_ReturnsEmpty)
{
    IpcClient client;
    client.Init();
    std::string cfg;
    EXPECT_NO_THROW(cfg = client.IpcClientNpuConfig());
    // Without server, cfg should be empty string
    EXPECT_EQ(cfg, "");
}

TEST_F(NpuIpcClientTest, IpcClientNpuConfig_OversizedMessageIsRejected)
{
    MOCKER_CPP(&IpcClient::SyncSendMessage).stubs().will(returnValue(true));
    MOCKER_CPP(&NpuIpcEndPoint<0>::TryPeekMessage).expects(exactly(MAX_IPC_RETRIES)).will(invoke(PeekOversizedMessage));
    MOCKER_CPP(&NpuIpcEndPoint<0>::TryRcvMessage).expects(exactly(MAX_IPC_RETRIES)).will(returnValue(true));
    MOCKER_CPP(&NpuIpcEndPoint<0>::GetName).expects(never());

    IpcClient client;
    EXPECT_TRUE(client.IpcClientNpuConfig().empty());
}

// 7. ipcName_ 初始化时机：构造函数内解析（改动将 GetDynoIpcName 从 Init() 前移到构造函数）
TEST_F(NpuIpcClientTest, Constructor_ResolvesIpcName_WithoutInit)
{
    // 构造函数调用 GetDynoIpcName()，因此 GetHostUid 恰好 1 次；
    // 改动前 ipcName_ 到 Init() 才解析，此时调用次数为 0，verify 抛异常即判定失败
    MOCKER_CPP(&GetHostUid).expects(exactly(1)).will(returnValue(std::string("4242")));
    IpcClient client;
    GlobalMockObject::verify();  // 构造后立即校验（verify 成功后同时 reset）
    // Init() 只刷新 pids_，不再调用 GetDynoIpcName
    EXPECT_TRUE(client.Init());
}

TEST_F(NpuIpcClientTest, RegisterInstance_BeforeInit_SendsToConstructorIpcName)
{
    MOCKER_CPP(&GetHostUid).stubs().will(returnValue(std::string("123")));
    // with() 按真实参数匹配（不含 this）：第 1 个是 message，第 2 个是 destName
    MOCKER_CPP(&IpcClient::SyncSendMessage)
        .stubs()
        .with(any(), spy(capturedDest_), any(), any())
        .will(returnValue(true));

    // 未调用 Init()：ipcName_ 应已在构造时就绪
    IpcClient client;
    EXPECT_TRUE(client.RegisterInstance(0));
    EXPECT_EQ(capturedDest_, DYNO_IPC_NAME + std::string("_123"));
    GlobalMockObject::verify();
}

TEST_F(NpuIpcClientTest, RegisterInstance_EmptyHostUid_BeforeInit_SendsToBaseName)
{
    MOCKER_CPP(&GetHostUid).stubs().will(returnValue(std::string("")));
    MOCKER_CPP(&IpcClient::SyncSendMessage)
        .stubs()
        .with(any(), spy(capturedDest_), any(), any())
        .will(returnValue(true));

    IpcClient client;
    EXPECT_TRUE(client.RegisterInstance(0));
    // hostUid 为空时构造出的 ipcName_ 退化为 dynolog
    EXPECT_EQ(capturedDest_, DYNO_IPC_NAME);
    GlobalMockObject::verify();
}

TEST_F(NpuIpcClientTest, Init_DoesNotRecomputeIpcName)
{
    // 构造消费第 1 个返回值；若 Init() 重新解析会取到第 2 个值（exactly(1) 也会失败）
    MOCKER_CPP(&GetHostUid).expects(exactly(1)).will(returnObjectList(std::string("first"), std::string("second")));
    MOCKER_CPP(&IpcClient::SyncSendMessage)
        .stubs()
        .with(any(), spy(capturedDest_), any(), any())
        .will(returnValue(true));

    IpcClient client;
    EXPECT_TRUE(client.Init());
    EXPECT_TRUE(client.RegisterInstance(0));
    EXPECT_EQ(capturedDest_, DYNO_IPC_NAME + std::string("_first"));
    GlobalMockObject::verify();
}

TEST_F(NpuIpcClientTest, SendNpuStatus_BeforeInit_SendsToConstructorIpcName)
{
    MOCKER_CPP(&GetHostUid).stubs().will(returnValue(std::string("31337")));
    MOCKER_CPP(&IpcClient::SyncSendMessage)
        .stubs()
        .with(any(), spy(capturedDest_), any(), any())
        .will(returnValue(true));

    IpcClient client;
    NpuStatus status;
    status.status = 0;
    status.currentStep = 1;
    status.pid = GetProcessId();
    EXPECT_TRUE(client.SendNpuStatus(status, MSG_TYPE_MONITOR_STATUS));
    EXPECT_EQ(capturedDest_, DYNO_IPC_NAME + std::string("_31337"));
    GlobalMockObject::verify();
}

TEST_F(NpuIpcClientTest, IpcClientNpuConfig_BeforeInit_SendsToConstructorIpcName)
{
    MOCKER_CPP(&GetHostUid).stubs().will(returnValue(std::string("555")));
    // 返回 false：立即走失败分支，避免 PollRecvMessage 轮询（5 × 10ms）
    MOCKER_CPP(&IpcClient::SyncSendMessage)
        .stubs()
        .with(any(), spy(capturedDest_), any(), any())
        .will(returnValue(false));

    IpcClient client;
    EXPECT_EQ(client.IpcClientNpuConfig(), "");
    EXPECT_EQ(capturedDest_, DYNO_IPC_NAME + std::string("_555"));
    GlobalMockObject::verify();
}

// 8. Message 构造模板：3 个重载的载荷/元数据正确性与超长类型异常分支（此前未覆盖）
TEST_F(NpuIpcClientTest, ConstructMessagePayloadsAndTypeLimit)
{
    NpuStatus status{};
    status.status = 7;
    auto msg = Message::ConstructMessage(status, MSG_TYPE_MONITOR_STATUS);
    EXPECT_EQ(msg->metadata.size, sizeof(status));
    EXPECT_STREQ(msg->metadata.type, "npuMonitorStatus");
    EXPECT_EQ(memcmp(msg->buf.get(), &status, sizeof(status)), 0);

    auto strMsg = Message::ConstructStrMessage("hello", MSG_TYPE_DATA);
    EXPECT_EQ(strMsg->metadata.size, 5u);
    EXPECT_STREQ(strMsg->metadata.type, "data");
    EXPECT_EQ(memcmp(strMsg->buf.get(), "hello", 5), 0);

    NpuContext ctxt{};
    ctxt.npu = 3;
    auto arrMsg = Message::ConstructMessage<NpuContext, int32_t>(ctxt, MSG_TYPE_CONTEXT, 2);
    EXPECT_EQ(arrMsg->metadata.size, sizeof(NpuContext) + sizeof(int32_t) * 2);

    // 类型串超出 metadata.type 容量：三个重载均抛 runtime_error
    // （模板实参中的逗号需加括号，否则会被 EXPECT_THROW 视为宏参数分隔）
    const std::string tooLong(TYPE_SIZE, 'x');
    EXPECT_THROW(Message::ConstructMessage(status, tooLong), std::runtime_error);
    EXPECT_THROW(Message::ConstructStrMessage("data", tooLong), std::runtime_error);
    EXPECT_THROW((Message::ConstructMessage<NpuContext, int32_t>(ctxt, tooLong, 2)), std::runtime_error);
}

// 9. 真实 IPC 往返：文件系统 socket 服务端接收注册/状态，并预投递配置回复，
// 解锁 SyncSendMessage/RegisterInstance/SendNpuStatus 成功路径与
// IpcClientNpuConfig → Recv → PollRecvMessage 全链路（此前仅 mock 或失败路径）
TEST_F(NpuIpcClientTest, RealSocketRegisterStatusAndConfigRoundTrip)
{
    MOCKER_CPP(&GetHostUid).stubs().will(returnValue(std::string("42123")));

    // 服务端绑定客户端目标文件系统路径：<home>/dynolog_42123.sock
    const std::string home = GetCurrentUserHomePath();
    ASSERT_FALSE(home.empty());
    const std::string sockPath = home + "/dynolog_42123.sock";
    int serverFd = socket(AF_UNIX, SOCK_DGRAM, 0);
    ASSERT_GE(serverFd, 0);
    struct sockaddr_un serverAddr
    {
    };
    serverAddr.sun_family = AF_UNIX;
    ASSERT_LT(sockPath.size(), sizeof(serverAddr.sun_path));
    strncpy(serverAddr.sun_path, sockPath.c_str(), sizeof(serverAddr.sun_path) - 1);
    unlink(sockPath.c_str());
    ASSERT_EQ(bind(serverFd, reinterpret_cast<struct sockaddr *>(&serverAddr), sizeof(serverAddr)), 0);
    chmod(sockPath.c_str(), 0600);

    auto recvMessage = [&serverFd](std::vector<char> &buf, struct sockaddr_un &from)
    {
        buf.assign(sizeof(Metadata) + sizeof(NpuStatus), '\0');
        struct iovec iov
        {
            buf.data(), buf.size()
        };
        struct msghdr mh
        {
        };
        mh.msg_name = &from;
        mh.msg_namelen = sizeof(from);
        mh.msg_iov = &iov;
        mh.msg_iovlen = 1;
        return recvmsg(serverFd, &mh, 0);
    };

    IpcClient client;
    EXPECT_TRUE(client.Init());
    EXPECT_TRUE(client.RegisterInstance(0));  // 真实 sendmsg 成功分支

    std::vector<char> buf;
    struct sockaddr_un clientAddr
    {
    };
    ASSERT_GT(recvMessage(buf, clientAddr), 0);
    auto *registerMeta = reinterpret_cast<Metadata *>(buf.data());
    EXPECT_EQ(registerMeta->size, sizeof(NpuContext));
    EXPECT_STREQ(registerMeta->type, "ctxt");
    auto *context = reinterpret_cast<NpuContext *>(buf.data() + sizeof(Metadata));
    EXPECT_EQ(context->npu, 0);
    EXPECT_EQ(context->pid, getpid());
    // 客户端来源为抽象地址（dynoconfigclient_<uuid>）
    ASSERT_EQ(clientAddr.sun_path[0], '\0');
    EXPECT_EQ(std::string(clientAddr.sun_path + 1).substr(0, 16), "dynoconfigclient");

    NpuStatus status{};
    status.status = 1;
    EXPECT_TRUE(client.SendNpuStatus(status, MSG_TYPE_MONITOR_STATUS));
    ASSERT_GT(recvMessage(buf, clientAddr), 0);
    EXPECT_STREQ(registerMeta->type, "npuMonitorStatus");

    // 按 Message 布局预投递配置回复：[Metadata][payload]，发往客户端抽象地址
    const std::string reply = "ok_config";
    Metadata replyMeta{};
    replyMeta.size = reply.size();
    memcpy(replyMeta.type, MSG_TYPE_DATA.c_str(), MSG_TYPE_DATA.size() + 1);
    struct iovec replyIov[2] = {{&replyMeta, sizeof(Metadata)}, {const_cast<char *>(reply.data()), reply.size()}};
    struct msghdr replyMh
    {
    };
    replyMh.msg_name = &clientAddr;
    replyMh.msg_namelen = sizeof(sa_family_t) + std::string(clientAddr.sun_path + 1).size() + 2;
    replyMh.msg_iov = replyIov;
    replyMh.msg_iovlen = 2;
    ASSERT_EQ(sendmsg(serverFd, &replyMh, 0), static_cast<ssize_t>(sizeof(Metadata) + reply.size()));

    // 发送请求后立即从队列取到预投回复：覆盖 Recv/TryPeek/TryRcv/GetName 成功链路
    EXPECT_EQ(client.IpcClientNpuConfig(), reply);

    close(serverFd);
    unlink(sockPath.c_str());
    GlobalMockObject::verify();
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
