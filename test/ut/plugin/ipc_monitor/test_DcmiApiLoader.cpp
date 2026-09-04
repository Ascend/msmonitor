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

#include "plugin/ipc_monitor/dcmi/DcmiApiLoader.h"

using dynolog_npu::ipc_monitor::dcmi::DcmiApiFuncs;
using dynolog_npu::ipc_monitor::dcmi::DcmiApiLoader;
using dynolog_npu::ipc_monitor::monitor::DcmiApiStatus;

namespace
{
// 假 v2 符号表：dcmiv2_get_device_list 返回 [0, kV2FakeDeviceCount) 扁平设备。
// 注：DcmiApiFuncs 成员为原生函数指针（对接 dlopen/dlsym），带捕获 lambda
// 无法隐式转为函数指针，故用静态函数 + 常量代替可变全局变量，保证测试隔离性。
constexpr int kV2FakeDeviceCount = 8;

int FakeV2GetDeviceList(int *devList, int *cnt, int listLen)
{
    const int n = kV2FakeDeviceCount < listLen ? kV2FakeDeviceCount : listLen;
    for (int i = 0; i < n; ++i)
    {
        devList[i] = i;
    }
    *cnt = n;
    return 0;  // DCMI_OK
}

DcmiApiFuncs MakeV2FakeFuncs()
{
    DcmiApiFuncs funcs;
    funcs.dcmiv2_init = []() -> int { return 0; };
    funcs.dcmiv2_get_device_list = &FakeV2GetDeviceList;
    return funcs;
}

}  // namespace

// v2 是 A5/950 等新代际的主路径：验证 IsV2/版本号与扁平拓扑枚举分支
TEST(DcmiApiLoaderTest, V2EnumeratesFlatDevices)
{
    DcmiApiLoader loader;
    ASSERT_EQ(loader.InjectForTestV2(MakeV2FakeFuncs()), DcmiApiStatus::OK);
    EXPECT_TRUE(loader.IsV2());
    EXPECT_EQ(loader.LoadInfo().version, "V2");
    EXPECT_EQ(loader.LoadInfo().cardNum, 8);
    EXPECT_EQ(loader.LoadInfo().deviceNumPerCard, 1);  // v2 扁平：每卡按 1 计

    auto devices = loader.ProbeAllDevices();
    ASSERT_EQ(devices.size(), 8u);
    for (uint32_t i = 0; i < devices.size(); ++i)
    {
        EXPECT_EQ(devices[i], i);
    }
    int cardId = -1;
    int devId = -1;
    ASSERT_TRUE(loader.MapDevId(5, cardId, devId));
    EXPECT_EQ(cardId, 0);  // v2 扁平模型：card 恒为 0
    EXPECT_EQ(devId, 5);   // flat == 设备号
}

// v2 枚举失败：InjectForTestV2 返回非 OK，保留 v2 模式标记便于诊断
TEST(DcmiApiLoaderTest, V2EnumerateFailureReported)
{
    DcmiApiFuncs funcs = MakeV2FakeFuncs();
    funcs.dcmiv2_get_device_list = [](int *, int *cnt, int) -> int
    {
        *cnt = 0;
        return -1;  // 非 DCMI_OK
    };
    DcmiApiLoader loader;
    EXPECT_NE(loader.InjectForTestV2(funcs), DcmiApiStatus::OK);
    EXPECT_TRUE(loader.IsV2());
    EXPECT_FALSE(loader.LoadInfo().error.empty());
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
