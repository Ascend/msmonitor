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

#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "dynolog/src/LibkinetoConfigManager.h"
#include "dynolog/src/tracing/NpuStatusMessage.h"

namespace dynolog
{
namespace
{

constexpr char kSep = '\x1e';
const int32_t kActivities = static_cast<int32_t>(LibkinetoConfigType::ACTIVITIES);
const int32_t kEvents = static_cast<int32_t>(LibkinetoConfigType::EVENTS);
const int32_t kBoth = kActivities | kEvents;
// Mirrors kMaxBatchFrameBytes in LibkinetoConfigManager.cpp.
constexpr size_t kIpcBudget = 3900;

std::string MakeConfig(const std::string& iters)
{
    return "PROFILE_ACTIVITIES=CPU,NPU\nACTIVITIES_ITERATIONS=" + iters + "\nWARMUP_ITERATIONS=1";
}

std::string MakeLargeConfig(size_t size)
{
    std::string config = "PROFILE_ACTIVITIES=CPU,NPU\nACTIVITIES_ITERATIONS=2\nPROFILE_AIC_METRICS=";
    if (config.size() < size)
    {
        config.append(size - config.size(), 'm');
    }
    return config;
}

std::vector<std::string> Split(const std::string& s, char delim)
{
    std::vector<std::string> out;
    size_t pos = 0;
    while (true)
    {
        const size_t next = s.find(delim, pos);
        out.push_back(next == std::string::npos ? s.substr(pos) : s.substr(pos, next - pos));
        if (next == std::string::npos)
        {
            break;
        }
        pos = next + 1;
    }
    return out;
}

std::string Trim(const std::string& s)
{
    const size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
    {
        return "";
    }
    const size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::string LineValue(const std::string& text, const std::string& key)
{
    const std::string prefix = key + "=";
    size_t pos = 0;
    while (true)
    {
        const size_t lineEnd = text.find('\n', pos);
        const std::string line = (lineEnd == std::string::npos) ? text.substr(pos) : text.substr(pos, lineEnd - pos);
        const size_t start = line.find_first_not_of(" \t\r");
        if (start != std::string::npos && line.compare(start, prefix.size(), prefix) == 0)
        {
            return Trim(line.substr(start + prefix.size()));
        }
        if (lineEnd == std::string::npos)
        {
            return "";
        }
        pos = lineEnd + 1;
    }
}

int32_t Register(const std::string& job, int32_t pid)
{
    return LibkinetoConfigManager::getInstance()->registerLibkinetoContext(job, pid, 0);
}

OnDemandBatch Peek(const std::string& job, int32_t pid, int32_t type = kActivities)
{
    return LibkinetoConfigManager::getInstance()->obtainOnDemandBatch(job, {pid}, type);
}

bool Ack(const std::string& job, int32_t pid, const OnDemandBatch& batch, int32_t type = kActivities)
{
    return LibkinetoConfigManager::getInstance()->ackOnDemandBatch(job, {pid}, type, batch);
}

uint64_t Fnv1a64(const std::string& data)
{
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (const unsigned char byte : data)
    {
        hash ^= byte;
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

std::string HeaderField(const std::string& head, const std::string& key) { return LineValue(head, key); }

// A process entry is created by the first poll, so call Poll() before SetConfig().
GpuProfilerResult SetConfig(const std::string& job, int32_t pid, const std::string& cfg, int32_t type = kActivities)
{
    return LibkinetoConfigManager::getInstance()->setOnDemandConfig(job, {pid}, cfg, type, 1000);
}

std::string Poll(const std::string& job, int32_t pid)
{
    const OnDemandBatch batch = Peek(job, pid);
    if (!batch.payload.empty())
    {
        (void)Ack(job, pid, batch);
    }
    return batch.payload;
}

TEST(LibkinetoConfigManagerTest, MultiMsgFraming)
{
    const std::string job = "ut-wire-frame";
    const int32_t pid = 11001;
    ASSERT_GE(Register(job, pid), 0);
    EXPECT_TRUE(Poll(job, pid).empty());

    const auto r1 = SetConfig(job, pid, MakeConfig("2"));
    const auto r2 = SetConfig(job, pid, MakeConfig("3"));
    ASSERT_EQ(1u, r1.traceIds.size());
    ASSERT_EQ(1u, r2.traceIds.size());
    EXPECT_NE(r1.traceIds[0], r2.traceIds[0]);

    const std::string resp = Poll(job, pid);
    EXPECT_NE(std::string::npos, resp.find("DYNO_BATCH_COUNT=2"));
    const auto parts = Split(resp, kSep);
    ASSERT_EQ(3u, parts.size());
    // Legacy body carries the newest config.
    EXPECT_EQ(r2.traceIds[0], LineValue(parts[0], "REQUEST_TRACE_ID"));
    EXPECT_EQ(r1.traceIds[0], LineValue(parts[1], "REQUEST_TRACE_ID"));
    EXPECT_EQ(r2.traceIds[0], LineValue(parts[2], "REQUEST_TRACE_ID"));
    EXPECT_TRUE(Poll(job, pid).empty());
}

TEST(LibkinetoConfigManagerTest, TraceIdsUniqueAcrossRapidRequests)
{
    const std::string job = "ut-trace-unique";
    const int32_t pid = 11004;
    ASSERT_GE(Register(job, pid), 0);
    EXPECT_TRUE(Poll(job, pid).empty());

    std::set<std::string> seen;
    for (int i = 0; i < 5; ++i)
    {
        const auto res = SetConfig(job, pid, MakeConfig("2"));
        ASSERT_EQ(1u, res.traceIds.size());
        seen.insert(res.traceIds[0]);
    }
    EXPECT_EQ(5u, seen.size());
    EXPECT_FALSE(Poll(job, pid).empty());
    EXPECT_TRUE(Poll(job, pid).empty());
}

TEST(LibkinetoConfigManagerTest, QueueBoundReportsBusy)
{
    const std::string job = "ut-queue-busy";
    const int32_t pid = 11005;
    ASSERT_GE(Register(job, pid), 0);
    EXPECT_TRUE(Poll(job, pid).empty());

    for (int i = 0; i < 32; ++i)
    {
        const auto res = SetConfig(job, pid, MakeConfig("2"));
        EXPECT_EQ(0, res.activityProfilersBusy);
        EXPECT_EQ(1u, res.activityProfilersTriggered.size());
    }
    const auto full = SetConfig(job, pid, MakeConfig("2"));
    EXPECT_EQ(1, full.activityProfilersBusy);
    EXPECT_TRUE(full.activityProfilersTriggered.empty());

    // 32 configs exceed one 4 KB reply, so they are drained in acked chunks.
    int seen = 0;
    for (int round = 0; round < 10 && seen < 32; ++round)
    {
        const OnDemandBatch batch = Peek(job, pid);
        ASSERT_FALSE(batch.payload.empty());
        EXPECT_LE(batch.payload.size(), 4096u);
        seen += static_cast<int>(batch.activityCount);
        ASSERT_TRUE(Ack(job, pid, batch));
    }
    EXPECT_EQ(32, seen);
    EXPECT_TRUE(Poll(job, pid).empty());
}

TEST(LibkinetoConfigManagerTest, EventConfigLeavesNoRoomForActivityFrame)
{
    const std::string job = "ut-budget-remaining";
    const int32_t pid = 12001;
    ASSERT_GE(Register(job, pid), 0);
    EXPECT_TRUE(Poll(job, pid).empty());

    // The activity config fits in an empty message.
    const auto activity = SetConfig(job, pid, MakeLargeConfig(1700), kActivities);
    ASSERT_EQ(1u, activity.activityProfilersTriggered.size());
    // A large event config alone is also allowed, but it consumes most of the budget.
    const auto event = SetConfig(job, pid, MakeLargeConfig(3800), kEvents);
    ASSERT_EQ(1u, event.eventProfilersTriggered.size());

    const OnDemandBatch first = Peek(job, pid, kBoth);
    EXPECT_TRUE(first.eventIncluded);
    EXPECT_EQ(0u, first.activityCount);
    EXPECT_LE(first.payload.size(), kIpcBudget);
    ASSERT_TRUE(Ack(job, pid, first, kBoth));

    // Once the event section is acked the deferred activity config is sent.
    const OnDemandBatch second = Peek(job, pid, kBoth);
    EXPECT_FALSE(second.eventIncluded);
    EXPECT_EQ(1u, second.activityCount);
    EXPECT_LE(second.payload.size(), kIpcBudget);
    EXPECT_TRUE(Ack(job, pid, second, kBoth));
    EXPECT_TRUE(Poll(job, pid).empty());
}

TEST(LibkinetoConfigManagerTest, OversizedConfigRejectedBeforeQueuing)
{
    ASSERT_GE(Register("ut-budget-combined", 12002), 0);
    EXPECT_TRUE(Poll("ut-budget-combined", 12002).empty());
    // Event section plus duplicated activity frame would exceed the IPC budget.
    const auto combined = SetConfig("ut-budget-combined", 12002, MakeLargeConfig(1500), kBoth);
    EXPECT_EQ(1u, combined.eventProfilersTriggered.size());
    EXPECT_TRUE(combined.activityProfilersTriggered.empty());
    EXPECT_EQ(1, combined.activityProfilersBusy);
    const OnDemandBatch batch = Peek("ut-budget-combined", 12002, kBoth);
    EXPECT_EQ(0u, batch.activityCount);
    EXPECT_LE(batch.payload.size(), kIpcBudget);
    EXPECT_TRUE(Ack("ut-budget-combined", 12002, batch, kBoth));

    ASSERT_GE(Register("ut-budget-event", 12003), 0);
    EXPECT_TRUE(Poll("ut-budget-event", 12003).empty());
    const auto event = SetConfig("ut-budget-event", 12003, MakeLargeConfig(kIpcBudget + 10), kEvents);
    EXPECT_TRUE(event.eventProfilersTriggered.empty());
    EXPECT_EQ(1, event.eventProfilersBusy);
    EXPECT_TRUE(Peek("ut-budget-event", 12003, kEvents).payload.empty());
}

TEST(LibkinetoConfigManagerTest, RepeatedPeekIsRetryable)
{
    const std::string job = "ut-ack";
    const int32_t pid = 11006;
    ASSERT_GE(Register(job, pid), 0);
    EXPECT_TRUE(Poll(job, pid).empty());

    SetConfig(job, pid, MakeConfig("2"));
    SetConfig(job, pid, MakeConfig("3"));

    const OnDemandBatch first = Peek(job, pid);
    ASSERT_FALSE(first.payload.empty());
    EXPECT_EQ(2u, first.activityCount);

    // Repeated peeks before an ack return the same batch; the queue never stalls.
    const OnDemandBatch second = Peek(job, pid);
    EXPECT_EQ(first.payload, second.payload);
    EXPECT_EQ(first.seq, second.seq);
    EXPECT_TRUE(Ack(job, pid, second));
    EXPECT_TRUE(Poll(job, pid).empty());
    // Duplicate acks must not drop configs queued afterwards.
    EXPECT_FALSE(Ack(job, pid, first));
    EXPECT_FALSE(Ack(job, pid, second));

    SetConfig(job, pid, MakeConfig("4"));
    const OnDemandBatch third = Peek(job, pid);
    EXPECT_EQ(1u, third.activityCount);
    EXPECT_EQ(2u, third.seq);
    EXPECT_TRUE(Ack(job, pid, third));
    EXPECT_TRUE(Poll(job, pid).empty());
}

TEST(LibkinetoConfigManagerTest, FrameMetadataMatchesPayload)
{
    const std::string job = "ut-frame-meta";
    const int32_t pid = 11007;
    ASSERT_GE(Register(job, pid), 0);
    EXPECT_TRUE(Poll(job, pid).empty());

    SetConfig(job, pid, MakeConfig("2"));
    SetConfig(job, pid, MakeConfig("3"));
    const OnDemandBatch batch = Peek(job, pid);
    const size_t pos = batch.payload.find(kSep);
    ASSERT_NE(std::string::npos, pos);
    const std::string head = batch.payload.substr(0, pos);
    const std::string payload = batch.payload.substr(pos + 1);

    EXPECT_EQ("2", HeaderField(head, "DYNO_BATCH_COUNT"));
    EXPECT_EQ(std::to_string(payload.size()), HeaderField(head, "DYNO_BATCH_LEN"));
    EXPECT_EQ(Fnv1a64(payload), std::stoull(HeaderField(head, "DYNO_BATCH_SUM"), nullptr, 16));
    EXPECT_TRUE(Ack(job, pid, batch));
}

TEST(LibkinetoConfigManagerTest, InvalidActivityConfigRejected)
{
    const std::string job = "ut-invalid";
    const int32_t pid = 11008;
    ASSERT_GE(Register(job, pid), 0);
    EXPECT_TRUE(Poll(job, pid).empty());

    const std::string separator_in_config = "PROFILE_ACTIVITIES=CPU\nX=" + std::string(1, kSep) + "junk";
    EXPECT_TRUE(SetConfig(job, pid, separator_in_config).activityProfilersTriggered.empty());
    EXPECT_TRUE(SetConfig(job, pid, separator_in_config).traceIds.empty());
    const std::string reserved_key = "PROFILE_ACTIVITIES=CPU\nDYNO_BATCH_COUNT=9";
    EXPECT_TRUE(SetConfig(job, pid, reserved_key).activityProfilersTriggered.empty());
    EXPECT_TRUE(SetConfig(job, pid, "").activityProfilersTriggered.empty());
    EXPECT_TRUE(Poll(job, pid).empty());
}

TEST(LibkinetoConfigManagerTest, StatusCarriesTraceAndQueueDepth)
{
    NpuStatus status;
    status.status = 1;
    status.currentStep = 12;
    status.startStep = 10;
    status.stopStep = 15;
    status.pid = 11009;
    status.jobId = 7;
    snprintf(status.traceId, sizeof(status.traceId), "%s", "trace-abc");
    status.queueDepth = 3;
    status.rejected = 2;
    status.extended = 1;

    LibkinetoConfigManager::getInstance()->updateNpuStatus(status, kLibkinetoTraceStatus);
    const NpuStatus got = LibkinetoConfigManager::getInstance()->getNpuTraceStatus();
    EXPECT_EQ(1, got.status);
    EXPECT_EQ(12, got.currentStep);
    EXPECT_STREQ("trace-abc", got.traceId);
    EXPECT_EQ(3, got.queueDepth);
    EXPECT_EQ(2, got.rejected);
    EXPECT_EQ(1, got.extended);
}

TEST(NpuStatusMessageTest, DecodesLegacyAndFullStatus)
{
    NpuStatus legacy{};
    legacy.status = 1;
    legacy.currentStep = 12;
    legacy.startStep = 10;
    legacy.stopStep = 15;
    legacy.pid = 11010;
    legacy.jobId = 7;

    NpuStatus decoded{};
    ASSERT_TRUE(decodeNpuStatus(&legacy, kNpuStatusLegacySize, decoded));
    EXPECT_EQ(1, decoded.status);
    EXPECT_EQ(12, decoded.currentStep);
    EXPECT_EQ(10, decoded.startStep);
    EXPECT_EQ(15, decoded.stopStep);
    EXPECT_EQ(11010, decoded.pid);
    // Fields appended after the legacy prefix stay at their defaults.
    EXPECT_STREQ("", decoded.traceId);
    EXPECT_EQ(0, decoded.extended);

    NpuStatus full{};
    full.status = 2;
    snprintf(full.traceId, sizeof(full.traceId), "%s", "trace-full");
    full.queueDepth = 4;
    full.rejected = 1;
    full.extended = 1;

    ASSERT_TRUE(decodeNpuStatus(&full, sizeof(full), decoded));
    EXPECT_EQ(2, decoded.status);
    EXPECT_STREQ("trace-full", decoded.traceId);
    EXPECT_EQ(4, decoded.queueDepth);
    EXPECT_EQ(1, decoded.rejected);
    EXPECT_EQ(1, decoded.extended);
}

TEST(NpuStatusMessageTest, RejectsInvalidPayload)
{
    const std::string shortPayload(kNpuStatusLegacySize - 1, '\0');
    NpuStatus decoded{};
    decoded.status = 1;
    decoded.currentStep = 42;
    EXPECT_FALSE(decodeNpuStatus(shortPayload.data(), shortPayload.size(), decoded));
    EXPECT_FALSE(decodeNpuStatus(nullptr, sizeof(NpuStatus), decoded));
    EXPECT_FALSE(decodeNpuStatus("", 0, decoded));
    // Rejected messages must not clobber the cached status.
    EXPECT_EQ(1, decoded.status);
    EXPECT_EQ(42, decoded.currentStep);
}

}  // namespace
}  // namespace dynolog

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
