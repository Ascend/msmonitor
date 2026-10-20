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

#include <set>
#include <unordered_map>
#include <vector>

#include "plugin/ipc_monitor/dcmi/DcmiApiLoader.h"
#include "plugin/ipc_monitor/dcmi/DcmiMetrics.h"

using dynolog_npu::ipc_monitor::dcmi::AicoreInfoMetric;
using dynolog_npu::ipc_monitor::dcmi::AicpuInfoMetric;
using dynolog_npu::ipc_monitor::dcmi::DcmiApiFuncs;
using dynolog_npu::ipc_monitor::dcmi::DcmiApiLoader;
using dynolog_npu::ipc_monitor::dcmi::DcmiProbeDevice;
using dynolog_npu::ipc_monitor::dcmi::DcmiSampleSink;
using dynolog_npu::ipc_monitor::dcmi::HbmInfoMetric;
using dynolog_npu::ipc_monitor::dcmi::MultiUtilMetric;
using dynolog_npu::ipc_monitor::dcmi::PowerMetric;
using dynolog_npu::ipc_monitor::dcmi::TempMetric;
using dynolog_npu::ipc_monitor::dcmi::UtilRateMetric;
using dynolog_npu::ipc_monitor::dcmi::detail::DcmiAicoreInfo;
using dynolog_npu::ipc_monitor::dcmi::detail::DcmiAicpuInfo;
using dynolog_npu::ipc_monitor::dcmi::detail::DcmiHbmInfo;
using dynolog_npu::ipc_monitor::dcmi::detail::DcmiMultiUtilizationInfo;
using dynolog_npu::ipc_monitor::dcmi::detail::FREQ_TYPE_AICORE_CURRENT;
using dynolog_npu::ipc_monitor::dcmi::detail::FREQ_TYPE_AICORE_RATED;
using dynolog_npu::ipc_monitor::dcmi::detail::UTIL_TYPE_AICORE;
using dynolog_npu::ipc_monitor::dcmi::detail::UTIL_TYPE_AICPU;
using dynolog_npu::ipc_monitor::monitor::DcmiApiStatus;
using dynolog_npu::ipc_monitor::monitor::DcmiKindStatus;
using dynolog_npu::ipc_monitor::monitor::DcmiMetricKind;

namespace
{

void LoadV1(DcmiApiLoader &loader, const DcmiApiFuncs &funcs)
{
    EXPECT_EQ(loader.InjectForTest(funcs, 1, 1), DcmiApiStatus::OK);
}

std::set<DcmiMetricKind> AllOf(const std::vector<DcmiMetricKind> &kinds)
{
    return std::set<DcmiMetricKind>(kinds.begin(), kinds.end());
}

}  // namespace

TEST(DcmiMetricsTest, SymbolMissingMarksInactive)
{
    DcmiApiLoader loader;
    LoadV1(loader, DcmiApiFuncs{});
    EXPECT_FALSE(loader.Available());
    DcmiProbeDevice probe{0, 0, 0};
    std::unordered_map<DcmiMetricKind, DcmiKindStatus> status;

    PowerMetric power;
    EXPECT_EQ(power.GroupName(), "Power");
    EXPECT_FALSE(power.Init(loader, AllOf(power.Provides()), probe, status));
    EXPECT_FALSE(status[DcmiMetricKind::Power].enabled);

    TempMetric temp;
    EXPECT_FALSE(temp.Init(loader, AllOf(temp.Provides()), probe, status));
    AicoreInfoMetric aicore;
    EXPECT_FALSE(aicore.Init(loader, AllOf(aicore.Provides()), probe, status));
    AicpuInfoMetric aicpu;
    EXPECT_FALSE(aicpu.Init(loader, {}, probe, status));
    EXPECT_FALSE(aicpu.Init(loader, AllOf(aicpu.Provides()), probe, status));
    HbmInfoMetric hbm;
    EXPECT_FALSE(hbm.Init(loader, AllOf(hbm.Provides()), probe, status));
    MultiUtilMetric multi;
    EXPECT_FALSE(multi.Init(loader, AllOf(multi.Provides()), probe, status));
    UtilRateMetric util;
    EXPECT_EQ(util.SampleEveryTicks(), 10u);
    EXPECT_FALSE(util.Init(loader, AllOf(util.Provides()), probe, status));
}

TEST(DcmiMetricsTest, ProbeFailureAndCollectError)
{
    DcmiApiFuncs funcs;
    funcs.dcmi_init = []() -> int { return 0; };
    funcs.dcmi_get_device_power_info = [](int, int, int *) -> int { return -1; };
    funcs.dcmi_get_device_temperature = [](int, int, int *) -> int { return -2; };
    funcs.dcmi_get_device_aicore_info = [](int, int, void *) -> int { return -3; };
    funcs.dcmi_get_device_aicpu_info = [](int, int, void *) -> int { return -4; };
    funcs.dcmi_get_device_hbm_info = [](int, int, void *) -> int { return -5; };
    funcs.dcmi_get_device_multi_utilization_rate = [](int, int, void *) -> int { return -6; };
    funcs.dcmi_get_device_utilization_rate = [](int, int, int, unsigned int *) -> int { return -7; };
    DcmiApiLoader loader;
    LoadV1(loader, funcs);
    EXPECT_TRUE(loader.Available());
    DcmiProbeDevice probe{0, 0, 0};
    std::unordered_map<DcmiMetricKind, DcmiKindStatus> status;

    PowerMetric power;
    EXPECT_FALSE(power.Init(loader, {DcmiMetricKind::Power}, probe, status));
    DcmiSampleSink sink;
    EXPECT_EQ(power.Collect(0, 0, 0, 1, sink), -1);

    TempMetric temp;
    EXPECT_FALSE(temp.Init(loader, {DcmiMetricKind::Temp}, probe, status));
    EXPECT_EQ(temp.Collect(0, 0, 0, 1, sink), -2);

    AicoreInfoMetric aicore;
    EXPECT_FALSE(aicore.Init(loader, AllOf(aicore.Provides()), probe, status));
    EXPECT_EQ(aicore.Collect(0, 0, 0, 1, sink), -3);

    AicpuInfoMetric aicpu;
    EXPECT_FALSE(aicpu.Init(loader, AllOf(aicpu.Provides()), probe, status));
    EXPECT_EQ(aicpu.Collect(0, 0, 0, 1, sink), -4);

    HbmInfoMetric hbm;
    EXPECT_FALSE(hbm.Init(loader, AllOf(hbm.Provides()), probe, status));
    EXPECT_EQ(hbm.Collect(0, 0, 0, 1, sink), -5);

    MultiUtilMetric multi;
    EXPECT_FALSE(multi.Init(loader, AllOf(multi.Provides()), probe, status));
    EXPECT_EQ(multi.Collect(0, 0, 0, 1, sink), -6);

    UtilRateMetric util;
    EXPECT_FALSE(util.Init(loader, AllOf(util.Provides()), probe, status));
}

TEST(DcmiMetricsTest, CollectSuccessAndSkipProbe)
{
    DcmiApiFuncs funcs;
    funcs.dcmi_init = []() -> int { return 0; };
    funcs.dcmi_get_device_power_info = [](int, int, int *out) -> int
    {
        *out = 250;
        return 0;
    };
    funcs.dcmi_get_device_temperature = [](int, int, int *out) -> int
    {
        *out = 42;
        return 0;
    };
    funcs.dcmi_get_device_aicore_info = [](int, int, void *out) -> int
    {
        auto *info = static_cast<DcmiAicoreInfo *>(out);
        info->freq = 1800;
        info->cur_freq = 900;
        return 0;
    };
    funcs.dcmi_get_device_aicpu_info = [](int, int, void *out) -> int
    {
        auto *info = static_cast<DcmiAicpuInfo *>(out);
        info->max_freq = 1500;
        info->cur_freq = 800;
        return 0;
    };
    funcs.dcmi_get_device_hbm_info = [](int, int, void *out) -> int
    {
        auto *info = static_cast<DcmiHbmInfo *>(out);
        info->freq = 1600;
        info->memory_usage = 10;
        info->memory_size = 64;
        info->bandwith_util_rate = 20;
        info->temp = 30;
        return 0;
    };
    funcs.dcmi_get_device_multi_utilization_rate = [](int, int, void *out) -> int
    {
        auto *info = static_cast<DcmiMultiUtilizationInfo *>(out);
        info->aicore_util = 11;
        info->aic_util = 22;
        info->aiv_util = 33;
        info->npu_util = 44;
        return 0;
    };
    funcs.dcmi_get_device_frequency = [](int, int, int type, unsigned int *out) -> int
    {
        *out = (type == FREQ_TYPE_AICORE_RATED) ? 1800u : 900u;
        return 0;
    };
    funcs.dcmi_get_device_utilization_rate = [](int, int, int type, unsigned int *out) -> int
    {
        if (type == UTIL_TYPE_AICPU)
        {
            return -1;
        }
        *out = static_cast<unsigned int>(type);
        return 0;
    };
    DcmiApiLoader loader;
    LoadV1(loader, funcs);
    DcmiProbeDevice skip{-1, -1, 0};
    DcmiProbeDevice probe{0, 0, 0};
    std::unordered_map<DcmiMetricKind, DcmiKindStatus> status;

    PowerMetric power;
    EXPECT_TRUE(power.Init(loader, {DcmiMetricKind::Power}, skip, status));
    EXPECT_TRUE(status[DcmiMetricKind::Power].enabled);
    DcmiSampleSink sink;
    EXPECT_EQ(power.Collect(0, 0, 1, 9, sink), 0);
    ASSERT_EQ(sink.size(), 1u);
    EXPECT_DOUBLE_EQ(sink[0].value, 25.0);

    TempMetric temp;
    EXPECT_FALSE(temp.Init(loader, {}, probe, status));
    EXPECT_TRUE(temp.Init(loader, {DcmiMetricKind::Temp}, probe, status));
    sink.clear();
    EXPECT_EQ(temp.Collect(0, 0, 2, 9, sink), 0);
    ASSERT_EQ(sink.size(), 1u);
    EXPECT_DOUBLE_EQ(sink[0].value, 42.0);

    AicoreInfoMetric aicore;
    EXPECT_FALSE(aicore.Init(loader, {}, probe, status));
    EXPECT_TRUE(aicore.Init(loader, {DcmiMetricKind::AICoreFreq}, probe, status));
    EXPECT_TRUE(aicore.Init(loader, AllOf(aicore.Provides()), probe, status));
    sink.clear();
    EXPECT_EQ(aicore.Collect(0, 0, 3, 9, sink), 0);
    EXPECT_EQ(sink.size(), 2u);

    AicpuInfoMetric aicpu;
    EXPECT_TRUE(aicpu.Init(loader, {DcmiMetricKind::AICPUFreq}, skip, status));
    EXPECT_TRUE(aicpu.Init(loader, AllOf(aicpu.Provides()), probe, status));
    sink.clear();
    EXPECT_EQ(aicpu.Collect(0, 0, 4, 9, sink), 0);
    EXPECT_EQ(sink.size(), 2u);

    HbmInfoMetric hbm;
    EXPECT_TRUE(hbm.Init(loader, {DcmiMetricKind::HBMFreq}, skip, status));
    EXPECT_TRUE(hbm.Init(loader, AllOf(hbm.Provides()), probe, status));
    sink.clear();
    EXPECT_EQ(hbm.Collect(0, 0, 5, 9, sink), 0);
    EXPECT_EQ(sink.size(), 5u);

    MultiUtilMetric multi;
    EXPECT_TRUE(multi.Init(loader, {DcmiMetricKind::NPUUtil}, skip, status));
    EXPECT_TRUE(multi.Init(loader, AllOf(multi.Provides()), probe, status));
    sink.clear();
    EXPECT_EQ(multi.Collect(0, 0, 6, 9, sink), 0);
    EXPECT_EQ(sink.size(), 4u);

    UtilRateMetric skipping;
    EXPECT_TRUE(skipping.Init(loader, {DcmiMetricKind::AICoreUtil, DcmiMetricKind::AICPUUtil}, skip, status));
    sink.clear();
    EXPECT_EQ(skipping.Collect(0, 0, 7, 9, sink), -1);
    EXPECT_GE(sink.size(), 1u);

    UtilRateMetric probed;
    status.clear();
    EXPECT_TRUE(probed.Init(loader, AllOf(probed.Provides()), probe, status));
    EXPECT_FALSE(status[DcmiMetricKind::AICPUUtil].enabled);
    EXPECT_TRUE(status[DcmiMetricKind::AICoreUtil].enabled);
    sink.clear();
    EXPECT_EQ(probed.Collect(0, 0, 7, 9, sink), 0);
    EXPECT_GE(sink.size(), 1u);
}

TEST(DcmiMetricsTest, AicoreFallsBackToFrequency)
{
    DcmiApiFuncs funcs;
    funcs.dcmi_get_device_frequency = [](int, int, int type, unsigned int *out) -> int
    {
        if (type == FREQ_TYPE_AICORE_CURRENT)
        {
            *out = 700;
            return 0;
        }
        if (type == FREQ_TYPE_AICORE_RATED)
        {
            return -1;
        }
        *out = 1;
        return 0;
    };
    DcmiApiLoader loader;
    LoadV1(loader, funcs);
    AicoreInfoMetric onlyCur;
    std::unordered_map<DcmiMetricKind, DcmiKindStatus> status;
    EXPECT_TRUE(onlyCur.Init(loader, {DcmiMetricKind::AICoreFreq}, DcmiProbeDevice{0, 0, 0}, status));
    DcmiSampleSink sink;
    EXPECT_EQ(onlyCur.Collect(0, 0, 0, 1, sink), 0);
    ASSERT_EQ(sink.size(), 1u);
    EXPECT_DOUBLE_EQ(sink[0].value, 700.0);

    AicoreInfoMetric both;
    EXPECT_TRUE(both.Init(loader, AllOf(both.Provides()), DcmiProbeDevice{0, 0, 0}, status));
    sink.clear();
    EXPECT_EQ(both.Collect(0, 0, 0, 1, sink), -1);

    DcmiApiFuncs failFreq;
    failFreq.dcmi_get_device_frequency = [](int, int, int, unsigned int *) -> int { return -1; };
    DcmiApiLoader failLoader;
    LoadV1(failLoader, failFreq);
    AicoreInfoMetric failed;
    EXPECT_FALSE(failed.Init(failLoader, AllOf(failed.Provides()), DcmiProbeDevice{0, 0, 0}, status));
}

TEST(DcmiMetricsTest, V2DispatchAndTopology)
{
    DcmiApiFuncs bad;
    bad.dcmiv2_get_device_list = [](int *, int *cnt, int) -> int
    {
        *cnt = 0;
        return 0;
    };
    DcmiApiLoader failed;
    EXPECT_NE(failed.InjectForTestV2(bad), DcmiApiStatus::OK);

    DcmiApiFuncs funcs;
    funcs.dcmiv2_init = []() -> int { return 0; };
    funcs.dcmiv2_get_device_list = [](int *list, int *cnt, int cap) -> int
    {
        if (cap < 1)
        {
            return -1;
        }
        list[0] = 7;
        *cnt = 1;
        return 0;
    };
    funcs.dcmiv2_get_device_power_info = [](int, int *out) -> int
    {
        *out = 10;
        return 0;
    };
    funcs.dcmiv2_get_device_temperature = [](int, int *out) -> int
    {
        *out = 11;
        return 0;
    };
    funcs.dcmiv2_get_device_frequency = [](int, int, unsigned int *out) -> int
    {
        *out = 12;
        return 0;
    };
    funcs.dcmiv2_get_device_utilization_rate = [](int, int, unsigned int *out) -> int
    {
        *out = 13;
        return 0;
    };
    funcs.dcmiv2_get_device_aicore_info = [](int, void *out) -> int
    {
        static_cast<DcmiAicoreInfo *>(out)->cur_freq = 14;
        return 0;
    };
    funcs.dcmiv2_get_device_aicpu_info = [](int, void *out) -> int
    {
        static_cast<DcmiAicpuInfo *>(out)->cur_freq = 15;
        return 0;
    };
    funcs.dcmiv2_get_device_hbm_info = [](int, void *out) -> int
    {
        static_cast<DcmiHbmInfo *>(out)->freq = 16;
        return 0;
    };
    funcs.dcmiv2_get_device_multi_utilization_rate = [](int, void *out) -> int
    {
        static_cast<DcmiMultiUtilizationInfo *>(out)->npu_util = 17;
        return 0;
    };

    DcmiApiLoader loader;
    ASSERT_EQ(loader.InjectForTestV2(funcs), DcmiApiStatus::OK);
    EXPECT_TRUE(loader.IsV2());
    EXPECT_TRUE(loader.Available());
    EXPECT_TRUE(loader.HasPower());
    EXPECT_TRUE(loader.HasTemperature());
    EXPECT_TRUE(loader.HasFrequency());
    EXPECT_TRUE(loader.HasUtilizationRate());
    EXPECT_TRUE(loader.HasAicoreInfo());
    EXPECT_TRUE(loader.HasAicpuInfo());
    EXPECT_TRUE(loader.HasHbmInfo());
    EXPECT_TRUE(loader.HasMultiUtilization());
    EXPECT_EQ(loader.ProbeAllDevices().size(), 1u);
    int card = -1;
    int dev = -1;
    EXPECT_TRUE(loader.MapDevId(0, card, dev));
    EXPECT_FALSE(loader.MapDevId(9, card, dev));
    loader.SetDevices({0});

    int power = 0;
    EXPECT_EQ(loader.GetPower(0, 0, 0, &power), 0);
    EXPECT_EQ(power, 10);
    int temp = 0;
    EXPECT_EQ(loader.GetTemperature(0, 0, 0, &temp), 0);
    unsigned int freq = 0;
    EXPECT_EQ(loader.GetFrequency(0, 0, 0, 1, &freq), 0);
    unsigned int util = 0;
    EXPECT_EQ(loader.GetUtilizationRate(0, 0, 0, UTIL_TYPE_AICORE, &util), 0);
    DcmiAicoreInfo aicore{};
    EXPECT_EQ(loader.GetAicoreInfo(0, 0, 0, &aicore), 0);
    DcmiAicpuInfo aicpu{};
    EXPECT_EQ(loader.GetAicpuInfo(0, 0, 0, &aicpu), 0);
    DcmiHbmInfo hbm{};
    EXPECT_EQ(loader.GetHbmInfo(0, 0, 0, &hbm), 0);
    DcmiMultiUtilizationInfo multi{};
    EXPECT_EQ(loader.GetMultiUtilization(0, 0, 0, &multi), 0);

    PowerMetric metric;
    std::unordered_map<DcmiMetricKind, DcmiKindStatus> status;
    EXPECT_TRUE(metric.Init(loader, {DcmiMetricKind::Power}, DcmiProbeDevice{0, 0, 0}, status));
    DcmiSampleSink sink;
    EXPECT_EQ(metric.Collect(0, 0, 0, 1, sink), 0);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
