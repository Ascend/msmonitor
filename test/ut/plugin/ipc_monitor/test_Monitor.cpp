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

#include "plugin/ipc_monitor/monitor/Monitor.h"
#include "plugin/ipc_monitor/mspti_monitor/MsptiMonitor.h"

using dynolog_npu::ipc_monitor::MsptiMonitor;
using dynolog_npu::ipc_monitor::monitor::API;
using dynolog_npu::ipc_monitor::monitor::DCMI_RING_CAPACITY;
using dynolog_npu::ipc_monitor::monitor::DcmiMetricKind;
using dynolog_npu::ipc_monitor::monitor::DcmiSample;
using dynolog_npu::ipc_monitor::monitor::Monitor;

// Monitor::Start 的 mspti kinds 分支（Report 路由与 DCMI 会话路径已有 test_MonitorProcessManager
// / test_DcmiMonitorSession 覆盖，此处只验证未覆盖部分）：
// 未支持 kind 过滤、kinds_ 装配、MsptiMonitor 拉起、运行中重复 Start 早退与重启
TEST(MonitorTest, MsptiKindsSessionStartFilterAndRestart)
{
    auto *monitor = Monitor::GetInstance();
    auto *mspti = MsptiMonitor::GetInstance();
    EXPECT_FALSE(mspti->IsStarted());

    // 未支持 kind（MEMORY）被过滤，仅 KERNEL 生效并拉起 MsptiMonitor
    monitor->Start({MSPTI_ACTIVITY_KIND_MEMORY, MSPTI_ACTIVITY_KIND_KERNEL});
    EXPECT_TRUE(mspti->IsStarted());
    EXPECT_EQ(monitor->GetKinds().size(), 1u);
    EXPECT_EQ(monitor->GetKinds().count(MSPTI_ACTIVITY_KIND_KERNEL), 1u);

    // 会话运行中重复 Start：直接返回，配置不变
    monitor->Start({MSPTI_ACTIVITY_KIND_API});
    EXPECT_EQ(monitor->GetKinds().size(), 1u);
    EXPECT_EQ(monitor->GetKinds().count(MSPTI_ACTIVITY_KIND_KERNEL), 1u);

    monitor->Stop();
    EXPECT_FALSE(mspti->IsStarted());

    // 停止后新会话：kinds 按新配置重建
    monitor->Start({MSPTI_ACTIVITY_KIND_API, MSPTI_ACTIVITY_KIND_ACL_API});
    EXPECT_EQ(monitor->GetKinds().size(), 2u);

    monitor->Stop();
    EXPECT_FALSE(mspti->IsStarted());
}

// ReportDcmiData 的满环覆盖分支：ring 打满后最旧样本被覆盖，DFX 溢出计数 +1
TEST(MonitorTest, DcmiRingOverflowCountsDrop)
{
    auto *monitor = Monitor::GetInstance();
    monitor->ClearDcmiDataForTest();

    constexpr size_t kOverflowPushes = DCMI_RING_CAPACITY + 1;
    for (size_t i = 0; i < kOverflowPushes; ++i)
    {
        DcmiSample sample{};
        sample.kind = static_cast<DcmiMetricKind>(i % 16);  // 0..15 均为合法 kind
        sample.timestampNs = i;
        sample.deviceId = 0;
        sample.value = 1.0;
        monitor->ReportDcmiData(std::move(sample));
    }

    auto snap = monitor->GetDcmiStatus();
    EXPECT_EQ(snap.ringSize, DCMI_RING_CAPACITY);
    EXPECT_EQ(snap.ringDropCount, 1u);

    monitor->ClearDcmiDataForTest();
    EXPECT_EQ(monitor->GetDcmiStatus().ringDropCount, 0u);
}

// ReportAPIData 对未支持 kind 走 default 分支：数据被丢弃，不入任何 API 容器
TEST(MonitorTest, ReportApiUnsupportedKindDropped)
{
    auto *monitor = Monitor::GetInstance();
    API api{};
    api.name = "unsupported";
    api.startNs = 100;
    api.endNs = 200;
    monitor->ReportAPIData(std::move(api), MSPTI_ACTIVITY_KIND_MEMORY);

    EXPECT_TRUE(monitor->GetAPIData().empty());
    EXPECT_TRUE(monitor->GetAclApiData().empty());
    EXPECT_TRUE(monitor->GetNodeApiData().empty());
    EXPECT_TRUE(monitor->GetRuntimeApiData().empty());
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
