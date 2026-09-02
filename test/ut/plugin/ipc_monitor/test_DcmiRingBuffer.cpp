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

#include "plugin/ipc_monitor/dcmi/DcmiRingBuffer.h"

using dynolog_npu::ipc_monitor::monitor::DcmiMetricKind;
using dynolog_npu::ipc_monitor::monitor::DcmiRingBuffer;
using dynolog_npu::ipc_monitor::monitor::DcmiSample;

namespace
{

DcmiSample MakeSample(uint64_t ts, double value) { return DcmiSample{DcmiMetricKind::Power, ts, 0, value}; }

}  // namespace

TEST(DcmiRingBufferTest, InitAndEmpty)
{
    DcmiRingBuffer ring;
    EXPECT_EQ(ring.Size(), 0u);
    EXPECT_EQ(ring.Capacity(), 0u);
    ring.Init(4);
    EXPECT_EQ(ring.Capacity(), 4u);
    EXPECT_EQ(ring.Size(), 0u);
    EXPECT_TRUE(ring.Snapshot().empty());
}

TEST(DcmiRingBufferTest, PushAndSnapshotOrder)
{
    DcmiRingBuffer ring;
    ring.Init(4);
    ring.Push(MakeSample(1, 1.0));
    ring.Push(MakeSample(2, 2.0));
    ring.Push(MakeSample(3, 3.0));
    auto snap = ring.Snapshot();
    ASSERT_EQ(snap.size(), 3u);
    EXPECT_EQ(snap[0].timestampNs, 1u);
    EXPECT_EQ(snap[1].timestampNs, 2u);
    EXPECT_EQ(snap[2].timestampNs, 3u);
}

TEST(DcmiRingBufferTest, OverwriteOldestAndDropFlag)
{
    DcmiRingBuffer ring;
    ring.Init(3);
    EXPECT_FALSE(ring.Push(MakeSample(1, 1.0)));
    EXPECT_FALSE(ring.Push(MakeSample(2, 2.0)));
    EXPECT_FALSE(ring.Push(MakeSample(3, 3.0)));
    // 满后继续 push 覆盖最旧，且返回 true（供 DFX 溢出计数）
    EXPECT_TRUE(ring.Push(MakeSample(4, 4.0)));
    EXPECT_EQ(ring.Size(), 3u);
    auto snap = ring.Snapshot();
    ASSERT_EQ(snap.size(), 3u);
    // 按时间序：2,3,4（1 被覆盖）
    EXPECT_EQ(snap[0].timestampNs, 2u);
    EXPECT_EQ(snap[1].timestampNs, 3u);
    EXPECT_EQ(snap[2].timestampNs, 4u);
}

TEST(DcmiRingBufferTest, ClearResets)
{
    DcmiRingBuffer ring;
    ring.Init(2);
    ring.Push(MakeSample(1, 1.0));
    ring.Push(MakeSample(2, 2.0));
    ring.Clear();
    EXPECT_EQ(ring.Size(), 0u);
    EXPECT_TRUE(ring.Snapshot().empty());
    // Clear 后可继续写入
    ring.Push(MakeSample(5, 5.0));
    EXPECT_EQ(ring.Size(), 1u);
}

TEST(DcmiRingBufferTest, ZeroCapacityNoOp)
{
    DcmiRingBuffer ring;
    ring.Init(0);
    EXPECT_FALSE(ring.Push(MakeSample(1, 1.0)));
    EXPECT_EQ(ring.Size(), 0u);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
