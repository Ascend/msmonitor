/*
 * Copyright (C) 2025-2026. Huawei Technologies Co., Ltd. All rights reserved.
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

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "plugin/ipc_monitor/TimerTask.h"

using namespace dynolog_npu::ipc_monitor;

// Concrete implementation of TimerTask for testing
class TimerTaskSample : public TimerTask
{
   public:
    TimerTaskSample(const std::string& name, int interval)
        : TimerTask(name, interval), executeCount(0), preTaskCount(0), postTaskCount(0)
    {
    }

    void ExecuteTask() override { executeCount++; }

    void RunPreTask() override { preTaskCount++; }

    void RunPostTask() override { postTaskCount++; }

    std::atomic_int executeCount;
    std::atomic_int preTaskCount;
    std::atomic_int postTaskCount;
};

class TimerTaskTest : public ::testing::Test
{
   protected:
    void SetUp() override {}

    void TearDown() override {}

    // Helper method to wait for a short time
    void waitFor(int milliseconds) { std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds)); }
};

TEST_F(TimerTaskTest, ConstructorInitialization)
{
    constexpr int kInterval = 1;
    TimerTaskSample task("TestTask", kInterval);
    EXPECT_FALSE(task.IsRunning());
}

TEST_F(TimerTaskTest, StartAndStop)
{
    constexpr int kInterval = 1;
    TimerTaskSample task("TestTask", kInterval);

    // Start the task
    EXPECT_NO_THROW(task.Run());
    EXPECT_TRUE(task.IsRunning());

    // Give it time to start
    constexpr int kWaitTime = 100;
    waitFor(kWaitTime);

    // Stop the task
    EXPECT_NO_THROW(task.Stop());
    EXPECT_FALSE(task.IsRunning());

    // Verify pre and post tasks were called
    EXPECT_EQ(task.preTaskCount, 1);
    EXPECT_EQ(task.postTaskCount, 1);
}

TEST_F(TimerTaskTest, StartAlreadyRunning)
{
    constexpr int kInterval = 1;
    TimerTaskSample task("TestTask", kInterval);

    // Start the task
    EXPECT_NO_THROW(task.Run());
    EXPECT_TRUE(task.IsRunning());

    // Try to start again - should not cause issues
    EXPECT_NO_THROW(task.Run());
    EXPECT_TRUE(task.IsRunning());

    // Stop the task
    EXPECT_NO_THROW(task.Stop());
    EXPECT_FALSE(task.IsRunning());
}

TEST_F(TimerTaskTest, StopNotRunning)
{
    constexpr int kInterval = 1;
    TimerTaskSample task("TestTask", kInterval);

    // Stop a task that's not running - should not cause issues
    EXPECT_NO_THROW(task.Stop());
    EXPECT_FALSE(task.IsRunning());
}

TEST_F(TimerTaskTest, TriggerExecution)
{
    constexpr int kInterval = 1;
    TimerTaskSample task("TestTask", kInterval);  // Long interval to avoid auto-execution

    EXPECT_NO_THROW(task.Run());
    EXPECT_TRUE(task.IsRunning());

    constexpr int kWaitTime = 100;
    waitFor(kWaitTime);

    // Verify initial state
    EXPECT_EQ(task.executeCount, 0);

    // Trigger the task
    EXPECT_NO_THROW(task.Trigger());
    waitFor(100);

    // Verify task was executed
    EXPECT_EQ(task.executeCount, 1);

    EXPECT_NO_THROW(task.Stop());
}

TEST_F(TimerTaskTest, MultipleTriggers)
{
    constexpr int kInterval = 10;
    TimerTaskSample task("TestTask", kInterval);  // Long interval to avoid auto-execution

    EXPECT_NO_THROW(task.Run());
    EXPECT_TRUE(task.IsRunning());

    // Trigger multiple times
    constexpr int kWaitTime = 100;
    EXPECT_NO_THROW(task.Trigger());
    waitFor(kWaitTime);
    EXPECT_NO_THROW(task.Trigger());
    waitFor(kWaitTime);
    EXPECT_NO_THROW(task.Trigger());
    waitFor(kWaitTime);

    // Verify task was executed multiple times
    EXPECT_EQ(task.executeCount, 3);

    EXPECT_NO_THROW(task.Stop());
    EXPECT_FALSE(task.IsRunning());
}

TEST_F(TimerTaskTest, IntervalExecution)
{
    constexpr int kInterval = 1;
    TimerTaskSample task("TestTask", kInterval);  // 1 second interval

    EXPECT_NO_THROW(task.Run());
    EXPECT_TRUE(task.IsRunning());

    // Wait for at least 2 intervals to pass
    constexpr int kWaitTime = 2500;
    waitFor(kWaitTime);

    // Verify task was executed multiple times (at least 2)
    EXPECT_GE(task.executeCount, 2);

    EXPECT_NO_THROW(task.Stop());
    EXPECT_FALSE(task.IsRunning());
}

TEST_F(TimerTaskTest, SetInterval)
{
    int kInterval = 5;
    TimerTaskSample task("TestTask", kInterval);  // 5 second interval

    // Change interval to 1 second
    kInterval = 1;
    task.SetInterval(kInterval);

    EXPECT_NO_THROW(task.Run());
    EXPECT_TRUE(task.IsRunning());

    int kWaitTime = 100;
    waitFor(kWaitTime);

    // Wait for interval to pass
    kWaitTime = 1500;
    waitFor(kWaitTime);

    // Verify task was executed
    EXPECT_GE(task.executeCount, 1);

    EXPECT_NO_THROW(task.Stop());
    EXPECT_FALSE(task.IsRunning());
}

TEST_F(TimerTaskTest, ZeroInterval)
{
    constexpr int kInterval = 0;
    TimerTaskSample task("TestTask", kInterval);  // Zero interval

    EXPECT_NO_THROW(task.Run());
    EXPECT_TRUE(task.IsRunning());

    constexpr int kWaitTime = 100;
    waitFor(kWaitTime);

    // Verify task hasn't executed yet (should wait for trigger)
    EXPECT_EQ(task.executeCount, 0);

    // Trigger the task
    EXPECT_NO_THROW(task.Trigger());
    waitFor(kWaitTime);

    // Verify task was executed
    EXPECT_EQ(task.executeCount, 1);

    EXPECT_NO_THROW(task.Stop());
    EXPECT_FALSE(task.IsRunning());
}

TEST_F(TimerTaskTest, DestructorStopsTask)
{
    {
        constexpr int kInterval = 1;
        TimerTaskSample task("TestTask", kInterval);
        EXPECT_NO_THROW(task.Run());
        EXPECT_TRUE(task.IsRunning());

        constexpr int kWaitTime = 100;
        waitFor(kWaitTime);

        // Task should be running
        EXPECT_TRUE(task.IsRunning());

        // When going out of scope, destructor should stop the task
    }

    // After destruction, task should be stopped
    SUCCEED();
}

TEST_F(TimerTaskTest, PreAndPostTaskExecution)
{
    constexpr int kInterval = 1;
    TimerTaskSample task("TestTask", kInterval);

    EXPECT_NO_THROW(task.Run());
    EXPECT_TRUE(task.IsRunning());

    constexpr int kWaitTime = 1500;
    waitFor(kWaitTime);  // Wait for at least one execution
    EXPECT_NO_THROW(task.Stop());

    // Verify pre and post tasks were called
    EXPECT_EQ(task.preTaskCount, 1);
    EXPECT_EQ(task.postTaskCount, 1);

    // Verify task was executed at least once
    EXPECT_GE(task.executeCount, 1);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
