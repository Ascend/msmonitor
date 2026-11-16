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

#include <atomic>
#include <string>

#include "plugin/ipc_monitor/utils/thread.h"

using namespace dynolog_npu::ipc_monitor;

namespace
{

// Thread 的具体实现：Run 仅计数，用于验证线程确已执行
class ThreadProbe : public Thread
{
   public:
    ThreadProbe() = default;

    void Run() override { runCount++; }

    std::atomic<int> runCount{0};
};

}  // namespace

TEST(ThreadTest, ThreadNameHandling)
{
    ThreadProbe probe;
    EXPECT_EQ(probe.GetThreadName(), "IPCMonitor");  // 默认名

    probe.SetThreadName("");  // 空名不覆盖
    EXPECT_EQ(probe.GetThreadName(), "IPCMonitor");

    probe.SetThreadName("UTThread");
    EXPECT_EQ(probe.GetThreadName(), "UTThread");
}

TEST(ThreadTest, StartJoinAndStop)
{
    ThreadProbe probe;

    EXPECT_EQ(probe.Start(), 0);
    EXPECT_EQ(probe.Join(), 0);  // Join 阻塞至线程退出，Run 必已执行
    EXPECT_GE(probe.runCount.load(), 1);

    EXPECT_EQ(probe.Start(), 0);  // 可再次启动新线程
    EXPECT_EQ(probe.Stop(), 0);   // Stop 等价于 Join
    EXPECT_GE(probe.runCount.load(), 2);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
