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

#include <chrono>
#include <set>
#include <thread>

#include "plugin/ipc_monitor/dcmi/DcmiCollector.h"
#include "plugin/ipc_monitor/dcmi/DcmiMetricRegistry.h"
#include "plugin/ipc_monitor/monitor/Monitor.h"

using dynolog_npu::ipc_monitor::dcmi::DcmiApiFuncs;
using dynolog_npu::ipc_monitor::dcmi::DcmiApiLoader;
using dynolog_npu::ipc_monitor::dcmi::DcmiCollector;
using dynolog_npu::ipc_monitor::dcmi::DcmiKindStatus;
using dynolog_npu::ipc_monitor::dcmi::DcmiMetricBase;
using dynolog_npu::ipc_monitor::dcmi::DcmiMetricRegistry;
using dynolog_npu::ipc_monitor::dcmi::DcmiSampleSink;
using dynolog_npu::ipc_monitor::monitor::DcmiMetricKind;
using dynolog_npu::ipc_monitor::monitor::DcmiSample;
using dynolog_npu::ipc_monitor::monitor::Monitor;

namespace
{

// 假 loader：dcmi_init 返回成功，1 卡 × 1 设备。
DcmiApiFuncs MakeFakeFuncs()
{
    DcmiApiFuncs funcs;
    funcs.dcmi_init = []() -> int { return 0; };
    return funcs;
}

// 假 metric：每拍产出 1 条 Power 样本；failCode != 0 时返回该错误码。
class FakePowerMetric : public DcmiMetricBase
{
   public:
    explicit FakePowerMetric(int failCode = 0) : failCode_(failCode) {}
    std::string GroupName() const override { return "FakePower"; }
    std::vector<DcmiMetricKind> Provides() const override { return {DcmiMetricKind::Power}; }
    bool Init(const DcmiApiLoader &api, const std::set<DcmiMetricKind> &enabled,
              const dynolog_npu::ipc_monitor::dcmi::DcmiProbeDevice &probe,
              std::unordered_map<DcmiMetricKind, DcmiKindStatus> &status) override
    {
        (void)api;
        (void)probe;
        status[DcmiMetricKind::Power] = DcmiKindStatus{true, ""};
        return enabled.count(DcmiMetricKind::Power) > 0;
    }
    int Collect(int cardId, int devId, uint32_t flatDevId, uint64_t tsNs, DcmiSampleSink &sink) override
    {
        (void)cardId;
        (void)devId;
        if (failCode_ != 0)
        {
            return failCode_;
        }
        sink.push_back(DcmiSample{DcmiMetricKind::Power, tsNs, static_cast<int32_t>(flatDevId), 1.0});
        return 0;
    }

   private:
    int failCode_;
};

}  // namespace

class DcmiCollectorTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        // 覆盖默认 Power 工厂为假 metric（本测试进程内有效）
        DcmiMetricRegistry::GetInstance()->Register(DcmiMetricKind::Power, []() -> std::unique_ptr<DcmiMetricBase>
                                                    { return std::make_unique<FakePowerMetric>(); });
        Monitor::GetInstance()->ClearDcmiDataForTest();
    }

    void TearDown() override
    {
        Monitor::GetInstance()->Stop();
        Monitor::GetInstance()->ClearDcmiDataForTest();
        // 恢复默认工厂（RegisterDefaults 无条件覆盖），避免假 metric 污染同进程其他用例
        DcmiMetricRegistry::GetInstance()->RegisterDefaults();
    }
};

TEST_F(DcmiCollectorTest, StartCollectsSamples)
{
    DcmiCollector collector;
    collector.ApiForTest().InjectForTest(MakeFakeFuncs(), 1, 1);
    ASSERT_TRUE(collector.Start({DcmiMetricKind::Power}, 5, {}));

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    collector.Stop();

    auto status = collector.GetStatus();
    EXPECT_GE(status.tickCount, 1u);
    EXPECT_GE(Monitor::GetInstance()->GetDcmiData().size(), 1u);
    EXPECT_TRUE(status.groupFailCounts.empty());
}

TEST_F(DcmiCollectorTest, CollectFailureCountedAndRateLimited)
{
    DcmiMetricRegistry::GetInstance()->Register(DcmiMetricKind::Power, []() -> std::unique_ptr<DcmiMetricBase>
                                                { return std::make_unique<FakePowerMetric>(42); });

    DcmiCollector collector;
    collector.ApiForTest().InjectForTest(MakeFakeFuncs(), 1, 1);
    ASSERT_TRUE(collector.Start({DcmiMetricKind::Power}, 5, {}));

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    collector.Stop();

    auto status = collector.GetStatus();
    ASSERT_EQ(status.groupFailCounts.size(), 1u);
    EXPECT_EQ(status.groupFailCounts[0].first, "FakePower");
    EXPECT_GE(status.groupFailCounts[0].second, 1u);
    ASSERT_EQ(status.lastErrorCodes.size(), 1u);
    EXPECT_EQ(status.lastErrorCodes[0].second, 42);
    // 失败时不产出样本
    EXPECT_TRUE(Monitor::GetInstance()->GetDcmiData().empty());
}

TEST_F(DcmiCollectorTest, InvalidDeviceRejectedGracefully)
{
    // 注入 loader，但 devices 指向不存在的扁平设备 → PrepareDevices 失败，Start 返回 false 不崩溃
    DcmiCollector collector;
    collector.ApiForTest().InjectForTest(MakeFakeFuncs(), 1, 1);
    EXPECT_FALSE(collector.Start({DcmiMetricKind::Power}, 5, {999}));
    EXPECT_FALSE(collector.IsRunning());
}

TEST_F(DcmiCollectorTest, StopIdempotent)
{
    DcmiCollector collector;
    collector.ApiForTest().InjectForTest(MakeFakeFuncs(), 1, 1);
    ASSERT_TRUE(collector.Start({DcmiMetricKind::Power}, 5, {}));
    collector.Stop();
    collector.Stop();  // 幂等
    EXPECT_FALSE(collector.IsRunning());
}

TEST_F(DcmiCollectorTest, NoMetricEnabledRejected)
{
    DcmiCollector collector;
    collector.ApiForTest().InjectForTest(MakeFakeFuncs(), 1, 1);
    // 未注册的 kind → 无 group 激活 → Start 返回 false
    EXPECT_FALSE(collector.Start({DcmiMetricKind::Voltage}, 5, {}));
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
