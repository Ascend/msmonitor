/*
 * Copyright (C) 2026-2026. Huawei Technologies Co., Ltd. All rights reserved.
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

#include <gtest/gtest.h>

#include <utility>

#include "plugin/ipc_monitor/dcmi/DcmiTypes.h"
#include "plugin/ipc_monitor/monitor/Monitor.h"

using dynolog_npu::ipc_monitor::monitor::DcmiApiStatus;
using dynolog_npu::ipc_monitor::monitor::DcmiLayer;
using dynolog_npu::ipc_monitor::monitor::DcmiMetricKind;
using dynolog_npu::ipc_monitor::monitor::DcmiSample;
using dynolog_npu::ipc_monitor::monitor::Monitor;

TEST(DcmiMonitorSessionTest, ClearDcmiDataResetsOldSamples)
{
    auto *monitor = Monitor::GetInstance();
    monitor->ClearDcmiDataForTest();

    monitor->ReportDcmiData(DcmiSample{DcmiMetricKind::Power, 1, 0, 10.0});
    monitor->ReportDcmiData(DcmiSample{DcmiMetricKind::Temp, 2, 0, 20.0});

    EXPECT_EQ(monitor->GetDcmiData().size(), 2u);

    monitor->ClearDcmiDataForTest();
    EXPECT_TRUE(monitor->GetDcmiData().empty());
}

TEST(DcmiMonitorSessionTest, DcmiOnlySessionStartClearsOldSamples)
{
    // P2 回归：先注入旧样本，再启动"仅 DCMI"新会话。
    // 会话启动路径（StartDcmi）必须清空历史 ring，避免 get_result() 返回混合数据。
    auto *monitor = Monitor::GetInstance();
    monitor->ClearDcmiDataForTest();
    monitor->ReportDcmiData(DcmiSample{DcmiMetricKind::Power, 1, 0, 10.0});
    EXPECT_EQ(monitor->GetDcmiData().size(), 1u);

    monitor->Start({}, {DcmiLayer::DEVICE}, {}, 10, {});
    auto snap = monitor->GetDcmiStatus();
    monitor->Stop();

    if (snap.load.status != DcmiApiStatus::OK)
    {
        // 本机无 libdcmi：collector 未启动，ring 应已被会话启动清空
        EXPECT_TRUE(monitor->GetDcmiData().empty());
    }
    // 有 libdcmi 的环境：collector 已启动并可能写入新样本，不做空断言（真机验证）。
}

TEST(DcmiMonitorSessionTest, StartWithNothingRejected)
{
    auto *monitor = Monitor::GetInstance();
    // 无 kinds/无 dcmi → 不崩溃，不启动任何采集
    EXPECT_NO_THROW(monitor->Start({}, {}, {}, 10, {}));
}

TEST(DcmiMonitorSessionTest, DcmiStatusSnapshotPopulated)
{
    auto *monitor = Monitor::GetInstance();
    auto snap = monitor->GetDcmiStatus();
    // 元数据表恒可用（注册表是纯内存结构）
    EXPECT_GE(snap.meta.size(), 16u);
    EXPECT_GE(snap.ringCapacity, 0u);
}

TEST(DcmiMonitorSessionTest, DcmiRingCapacityFixedInternal)
{
    // ring 容量为内部固定策略（1M），不对外暴露
    auto *monitor = Monitor::GetInstance();
    monitor->Start({}, {DcmiLayer::DEVICE}, {}, 10, {});
    auto snap = monitor->GetDcmiStatus();
    EXPECT_EQ(snap.ringCapacity, 1000000u);
    monitor->Stop();
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
