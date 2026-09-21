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

#include "plugin/ipc_monitor/metric/MetricManager.h"

using namespace dynolog_npu::ipc_monitor;
using namespace dynolog_npu::ipc_monitor::metric;

TEST(MetricManagerTest, ConsumeReturnsNotSupportForNullProcessor)
{
    MetricManager manager;
    const auto kind = MSPTI_ACTIVITY_KIND_EXTERNAL_CORRELATION;
    msptiActivity record{kind};

    manager.EnableKindSwitch(kind, true);
    EXPECT_EQ(manager.ConsumeMsptiData(&record), ErrCode::NOT_SUPPORT);
}

TEST(MetricManagerTest, SendMetricMsgSkipsNullProcessor)
{
    MetricManager manager;
    manager.EnableKindSwitch(MSPTI_ACTIVITY_KIND_EXTERNAL_CORRELATION, true);
    EXPECT_NO_THROW(manager.ExecuteTask());
}

TEST(MetricManagerTest, RunPostTaskSkipsNullProcessor)
{
    MetricManager manager;
    manager.EnableKindSwitch(MSPTI_ACTIVITY_KIND_EXTERNAL_CORRELATION, true);
    EXPECT_NO_THROW(manager.RunPostTask());
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
