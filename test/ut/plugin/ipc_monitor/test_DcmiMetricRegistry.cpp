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

#include <set>

#include "plugin/ipc_monitor/dcmi/DcmiMetricRegistry.h"
#include "plugin/ipc_monitor/dcmi/DcmiMetrics.h"

using dynolog_npu::ipc_monitor::dcmi::DcmiMetricRegistry;
using dynolog_npu::ipc_monitor::monitor::DcmiLayer;
using dynolog_npu::ipc_monitor::monitor::DcmiMetricKind;

TEST(DcmiMetricRegistryTest, LayerExpansion)
{
    auto *registry = DcmiMetricRegistry::GetInstance();
    // DEVICE 层 = Power + Temp
    auto deviceKinds = registry->KindsForLayer(DcmiLayer::DEVICE);
    EXPECT_TRUE(deviceKinds.count(DcmiMetricKind::Power) > 0);
    EXPECT_TRUE(deviceKinds.count(DcmiMetricKind::Temp) > 0);
    EXPECT_EQ(deviceKinds.size(), 2u);
    // AICORE 层包含频率与利用率族
    auto aicoreKinds = registry->KindsForLayer(DcmiLayer::AICORE);
    EXPECT_TRUE(aicoreKinds.count(DcmiMetricKind::AICoreFreq) > 0);
    EXPECT_TRUE(aicoreKinds.count(DcmiMetricKind::AICoreUtil) > 0);
    // HBM 层包含带宽/容量/用量
    auto hbmKinds = registry->KindsForLayer(DcmiLayer::HBM);
    EXPECT_TRUE(hbmKinds.count(DcmiMetricKind::HBMBandwidth) > 0);
    EXPECT_TRUE(hbmKinds.count(DcmiMetricKind::HBMMemUsed) > 0);
}

TEST(DcmiMetricRegistryTest, CreateDedupeGroups)
{
    auto *registry = DcmiMetricRegistry::GetInstance();
    // AICORE 层：AicoreInfoMetric(2 kind) + MultiUtilMetric(4 kind)
    auto metrics = registry->Create(registry->KindsForLayer(DcmiLayer::AICORE));
    ASSERT_EQ(metrics.size(), 2u);
    EXPECT_EQ(metrics[0]->GroupName(), "AicoreInfo");
    EXPECT_EQ(metrics[1]->GroupName(), "MultiUtil");
}

TEST(DcmiMetricRegistryTest, UnknownKindIgnored)
{
    auto *registry = DcmiMetricRegistry::GetInstance();
    std::set<DcmiMetricKind> enabled = {DcmiMetricKind::Power, static_cast<DcmiMetricKind>(99)};
    auto metrics = registry->Create(enabled);
    ASSERT_EQ(metrics.size(), 1u);
    EXPECT_EQ(metrics[0]->GroupName(), "Power");
}

TEST(DcmiMetricRegistryTest, EmptyEnabledYieldsEmpty)
{
    auto *registry = DcmiMetricRegistry::GetInstance();
    std::set<DcmiMetricKind> enabled;
    EXPECT_TRUE(registry->Create(enabled).empty());
}

TEST(DcmiMetricRegistryTest, MetaComplete)
{
    auto *registry = DcmiMetricRegistry::GetInstance();
    auto allMeta = registry->AllMeta();
    // 每个可用 kind 都有元数据（含层/展示名/单位）
    for (auto kind : registry->KindsForLayer(DcmiLayer::AICORE))
    {
        auto *meta = registry->FindMeta(kind);
        ASSERT_NE(meta, nullptr);
        EXPECT_EQ(meta->layer, DcmiLayer::AICORE);
        EXPECT_FALSE(meta->displayName.empty());
        EXPECT_FALSE(meta->unit.empty());
    }
    auto *power = registry->FindMeta(DcmiMetricKind::Power);
    ASSERT_NE(power, nullptr);
    EXPECT_EQ(power->layer, DcmiLayer::DEVICE);
    EXPECT_EQ(power->displayName, "Power");
    EXPECT_EQ(power->unit, "W");
    EXPECT_GE(allMeta.size(), 16u);
}

TEST(DcmiMetricRegistryTest, CreateUtilRateFallback)
{
    auto *registry = DcmiMetricRegistry::GetInstance();
    std::set<DcmiMetricKind> enabled = {DcmiMetricKind::AICoreUtil, DcmiMetricKind::AICPUUtil};
    auto fb = registry->CreateUtilRate(enabled);
    ASSERT_NE(fb, nullptr);
    EXPECT_EQ(fb->GroupName(), "UtilRate");
    // 不包含非 util kind 时返回空
    std::set<DcmiMetricKind> powerOnly = {DcmiMetricKind::Power};
    EXPECT_EQ(registry->CreateUtilRate(powerOnly), nullptr);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
