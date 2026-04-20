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
#include <thread>
#include <chrono>
#include <vector>
#include <memory>
#include "plugin/ipc_monitor/utils/RingBuffer.h"

using namespace dynolog_npu::ipc_monitor;

class RingBufferTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F(RingBufferTest, ConstructorInitialization)
{
    RingBuffer<int> buffer;
    EXPECT_EQ(buffer.Size(), 0); // Should be empty after construction
}

TEST_F(RingBufferTest, InitAndUnInit)
{
    RingBuffer<int> buffer;

    // Initialize with capacity of 4
    constexpr int capacity = 4;
    EXPECT_NO_THROW(buffer.Init(capacity));
    // After init, buffer should be ready to use
    EXPECT_EQ(buffer.Size(), 0); // Should be empty after init

    // Uninit should clear everything
    EXPECT_NO_THROW(buffer.UnInit());
    EXPECT_EQ(buffer.Size(), 0); // Should be empty after uninit
}

TEST_F(RingBufferTest, BasicPushPop)
{
    RingBuffer<int> buffer;
    constexpr int capacity = 4;
    EXPECT_NO_THROW(buffer.Init(capacity));

    // Test basic push and pop operations
    constexpr int value = 42;
    EXPECT_TRUE(buffer.Push(value));

    int popped_value;
    EXPECT_TRUE(buffer.Pop(popped_value));
    EXPECT_EQ(popped_value, value);

    EXPECT_EQ(buffer.Size(), 0); // Should be empty after pop
    EXPECT_NO_THROW(buffer.UnInit());
}

TEST_F(RingBufferTest, MultiplePushPop)
{
    RingBuffer<int> buffer;
    constexpr int capacity = 4;
    EXPECT_NO_THROW(buffer.Init(capacity));

    constexpr int numValues = 3;
    // Push multiple values
    for (int i = 0; i < numValues; ++i) {
        EXPECT_TRUE(buffer.Push(i));
    }

    // Verify size
    EXPECT_EQ(buffer.Size(), numValues);

    // Pop all values
    for (int i = 0; i < numValues; ++i) {
        int value;
        EXPECT_TRUE(buffer.Pop(value));
        EXPECT_EQ(value, i);
    }

    // Buffer should be empty now
    EXPECT_EQ(buffer.Size(), 0);

    int dummy;
    EXPECT_FALSE(buffer.Pop(dummy)); // Should return false when empty
    EXPECT_NO_THROW(buffer.UnInit());
}

TEST_F(RingBufferTest, RingBehavior)
{
    RingBuffer<int> buffer;
    constexpr int capacity = 4;
    EXPECT_NO_THROW(buffer.Init(capacity)); // Capacity of 4

    // Fill the buffer up to capacity limit
    constexpr int numValues = 3;
    for (int i = 0; i < numValues; ++i) {  // Only push 3 items to respect ring buffer logic
        EXPECT_TRUE(buffer.Push(i));
    }

    // Try to push one more - should fail when buffer appears full due to ring logic
    int value = 999;
    EXPECT_FALSE(buffer.Push(value));

    // Pop one item
    EXPECT_TRUE(buffer.Pop(value));
    EXPECT_EQ(value, 0);

    // Now we should be able to push again
    value = 42;
    EXPECT_TRUE(buffer.Push(value));

    // Pop remaining items
    EXPECT_TRUE(buffer.Pop(value));
    EXPECT_EQ(value, 1);

    EXPECT_TRUE(buffer.Pop(value));
    EXPECT_EQ(value, 2);

    EXPECT_TRUE(buffer.Pop(value));
    EXPECT_EQ(value, 42);

    EXPECT_EQ(buffer.Size(), 0);
    EXPECT_NO_THROW(buffer.UnInit());
}

TEST_F(RingBufferTest, SizeCalculation)
{
    RingBuffer<int> buffer;
    constexpr int capacity = 4;
    EXPECT_NO_THROW(buffer.Init(capacity));

    EXPECT_EQ(buffer.Size(), 0);

    // Add elements and check size
    EXPECT_TRUE(buffer.Push(1));
    EXPECT_EQ(buffer.Size(), 1);

    EXPECT_TRUE(buffer.Push(2));
    EXPECT_EQ(buffer.Size(), 2);

    // Pop one and verify size decreases
    int dummy;
    EXPECT_TRUE(buffer.Pop(dummy));
    EXPECT_EQ(buffer.Size(), 1);

    // Pop another
    EXPECT_TRUE(buffer.Pop(dummy));
    EXPECT_EQ(buffer.Size(), 0);
    EXPECT_NO_THROW(buffer.UnInit());
}

TEST_F(RingBufferTest, StringType)
{
    RingBuffer<std::string> buffer;
    constexpr int capacity = 4;
    EXPECT_NO_THROW(buffer.Init(capacity));

    std::string str1 = "hello";
    std::string str2 = "world";

    EXPECT_TRUE(buffer.Push(str1));
    EXPECT_TRUE(buffer.Push(str2));

    EXPECT_EQ(buffer.Size(), 2);

    std::string popped_str;
    EXPECT_TRUE(buffer.Pop(popped_str));
    EXPECT_EQ(popped_str, "hello");

    EXPECT_TRUE(buffer.Pop(popped_str));
    EXPECT_EQ(popped_str, "world");
    EXPECT_EQ(buffer.Size(), 0);
    EXPECT_NO_THROW(buffer.UnInit());
}

TEST_F(RingBufferTest, ThreadSafetyProducerConsumer)
{
    const int capacity = 128;
    RingBuffer<int> buffer;
    buffer.Init(capacity);

    constexpr int num_items = 50;

    // Producer thread
    std::thread producer([&buffer, num_items]() {
        for (int i = 0; i < num_items; ++i) {
            bool pushed = false;
            while (!pushed) {
                pushed = buffer.Push(i);
                if (!pushed) {
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
            }
        }
    });

    // Consumer thread
    std::thread consumer([&buffer, num_items]() {
        int received = 0;
        while (received < num_items) {
            int value;
            if (buffer.Pop(value)) {
                received++;
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        }
    });

    producer.join();
    consumer.join();

    // Buffer should be empty after both threads finish
    EXPECT_EQ(buffer.Size(), 0);
    EXPECT_NO_THROW(buffer.UnInit());
}

TEST_F(RingBufferTest, FullCapacityHandling)
{
    RingBuffer<int> buffer;
    constexpr int capacity = 4;
    EXPECT_NO_THROW(buffer.Init(capacity)); // Capacity of 4

    // Fill up to the effective capacity
    constexpr int numValues = 3;
    for (int i = 0; i < numValues; ++i) {  // Ring buffer typically holds capacity-1 items effectively
        EXPECT_TRUE(buffer.Push(i));
    }

    // Should not be able to push more items when "full"
    EXPECT_FALSE(buffer.Push(99));

    // Pop one item
    int value;
    EXPECT_TRUE(buffer.Pop(value));
    EXPECT_EQ(value, 0);

    // Now should be able to push again
    EXPECT_TRUE(buffer.Push(99));

    // Should not be able to push more when full again
    EXPECT_FALSE(buffer.Push(100));
    EXPECT_NO_THROW(buffer.UnInit());
}

TEST_F(RingBufferTest, UninitializedBehavior)
{
    RingBuffer<int> buffer;  // Not initialized yet

    // Operations on uninitialized buffer should fail gracefully
    EXPECT_FALSE(buffer.Push(1));

    int dummy;
    EXPECT_FALSE(buffer.Pop(dummy));

    // Initialize buffer with capacity 4
    constexpr int capacity = 4;
    EXPECT_NO_THROW(buffer.Init(capacity));

    // Now operations should work
    EXPECT_TRUE(buffer.Push(1));
    EXPECT_TRUE(buffer.Pop(dummy));

    // Uninitialize buffer
    EXPECT_NO_THROW(buffer.UnInit());

    // Operations should fail again
    EXPECT_FALSE(buffer.Push(1));
    EXPECT_FALSE(buffer.Pop(dummy));
}

TEST_F(RingBufferTest, LargeCapacityTest)
{
    RingBuffer<int> buffer;
    constexpr int capacity = 1024; // Large capacity
    EXPECT_NO_THROW(buffer.Init(capacity));

    // Fill the buffer
    constexpr int numValues = 1023;
    for (int i = 0; i < numValues; ++i) {  // Don't fill completely due to ring logic
        EXPECT_TRUE(buffer.Push(i));
    }

    // Should not be able to add more
    EXPECT_FALSE(buffer.Push(-1));

    // Empty the buffer
    for (int i = 0; i < numValues; ++i) {
        int value;
        EXPECT_TRUE(buffer.Pop(value));
        EXPECT_EQ(value, i);
    }

    EXPECT_EQ(buffer.Size(), 0);
    EXPECT_NO_THROW(buffer.UnInit());
}

TEST_F(RingBufferTest, MoveSemantics)
{
    RingBuffer<std::unique_ptr<int>> buffer;
    constexpr int capacity = 4;
    EXPECT_NO_THROW(buffer.Init(capacity));

    auto ptr1 = std::make_unique<int>(42);
    auto ptr2 = std::make_unique<int>(84);

    // Push unique_ptrs using move semantics
    EXPECT_TRUE(buffer.Push(std::move(ptr1)));
    EXPECT_TRUE(buffer.Push(std::move(ptr2)));

    EXPECT_EQ(buffer.Size(), 2);

    // Pop the unique_ptrs
    std::unique_ptr<int> popped_ptr1, popped_ptr2;
    EXPECT_TRUE(buffer.Pop(popped_ptr1));
    EXPECT_TRUE(buffer.Pop(popped_ptr2));

    // Verify the values
    EXPECT_EQ(*popped_ptr1, 42);
    EXPECT_EQ(*popped_ptr2, 84);

    EXPECT_EQ(buffer.Size(), 0);
    EXPECT_NO_THROW(buffer.UnInit());
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
