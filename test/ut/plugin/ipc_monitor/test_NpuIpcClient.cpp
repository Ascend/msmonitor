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
#include <unistd.h>

#include <mockcpp/mockcpp.hpp>
#include <string>

#include "plugin/ipc_monitor/NpuIpcClient.h"
#include "plugin/ipc_monitor/utils/utils.h"

using namespace dynolog_npu::ipc_monitor;

namespace
{
bool PeekOversizedMessage(NpuIpcEndPointCtxt<0> &ctxt)
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
    MOCKER_CPP(&NpuIpcEndPoint<0>::TryPeekMessage).stubs().will(invoke(PeekOversizedMessage));
    MOCKER_CPP(&NpuIpcEndPoint<0>::TryRcvMessage).stubs().will(returnValue(true));
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

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
