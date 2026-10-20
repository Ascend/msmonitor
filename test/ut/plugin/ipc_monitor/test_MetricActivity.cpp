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

#include <string>
#include <unordered_map>

#include "plugin/ipc_monitor/metric/MetricApiProcess.h"
#include "plugin/ipc_monitor/metric/MetricCommunicationProcess.h"
#include "plugin/ipc_monitor/metric/MetricHcclProcess.h"
#include "plugin/ipc_monitor/metric/MetricKernelProcess.h"
#include "plugin/ipc_monitor/metric/MetricManager.h"
#include "plugin/ipc_monitor/metric/MetricMarkProcess.h"
#include "plugin/ipc_monitor/metric/MetricMemCpyProcess.h"
#include "plugin/ipc_monitor/metric/MetricMemProcess.h"
#include "plugin/ipc_monitor/metric/MetricMemSetProcess.h"

using namespace dynolog_npu::ipc_monitor;
using namespace dynolog_npu::ipc_monitor::metric;

namespace
{

template <typename T>
void Consume(T &proc, void *record)
{
    proc.ConsumeMsptiData(static_cast<msptiActivity *>(record));
}

msptiActivityKernel MakeKernel(uint32_t device, uint64_t start, uint64_t end)
{
    msptiActivityKernel rec{};
    rec.kind = MSPTI_ACTIVITY_KIND_KERNEL;
    rec.start = start;
    rec.end = end;
    rec.ds.deviceId = device;
    rec.ds.streamId = 1;
    rec.type = "vec";
    rec.name = "kernel";
    return rec;
}

msptiActivityApi MakeApi(uint64_t start, uint64_t end)
{
    msptiActivityApi rec{};
    rec.kind = MSPTI_ACTIVITY_KIND_API;
    rec.start = start;
    rec.end = end;
    rec.pt.processId = 1;
    rec.pt.threadId = 2;
    rec.name = "launch";
    return rec;
}

msptiActivityHccl MakeHccl(uint32_t device, uint64_t start, uint64_t end)
{
    msptiActivityHccl rec{};
    rec.kind = MSPTI_ACTIVITY_KIND_HCCL;
    rec.start = start;
    rec.end = end;
    rec.ds.deviceId = device;
    rec.name = "allreduce";
    rec.commName = "group";
    return rec;
}

msptiActivityMemory MakeMem(uint32_t device, uint64_t start, uint64_t end)
{
    msptiActivityMemory rec{};
    rec.kind = MSPTI_ACTIVITY_KIND_MEMORY;
    rec.start = start;
    rec.end = end;
    rec.deviceId = device;
    return rec;
}

msptiActivityMemset MakeMemSet(uint32_t device, uint64_t start, uint64_t end)
{
    msptiActivityMemset rec{};
    rec.kind = MSPTI_ACTIVITY_KIND_MEMSET;
    rec.start = start;
    rec.end = end;
    rec.deviceId = device;
    return rec;
}

msptiActivityMemcpy MakeMemCpy(uint32_t device, uint64_t start, uint64_t end)
{
    msptiActivityMemcpy rec{};
    rec.kind = MSPTI_ACTIVITY_KIND_MEMCPY;
    rec.start = start;
    rec.end = end;
    rec.deviceId = device;
    return rec;
}

msptiActivityCommunication MakeComm(uint32_t device, uint64_t start, uint64_t end)
{
    msptiActivityCommunication rec{};
    rec.kind = MSPTI_ACTIVITY_KIND_COMMUNICATION;
    rec.start = start;
    rec.end = end;
    rec.ds.deviceId = device;
    rec.name = "allgather";
    rec.commName = "g";
    rec.algType = "ring";
    return rec;
}

msptiActivityMarker MakeMarker(msptiActivityFlag flag, msptiActivitySourceKind source, uint64_t id, uint64_t ts,
                               uint32_t device)
{
    msptiActivityMarker rec{};
    rec.kind = MSPTI_ACTIVITY_KIND_MARKER;
    rec.flag = flag;
    rec.sourceKind = source;
    rec.timestamp = ts;
    rec.id = id;
    rec.name = "range";
    rec.domain = "domain";
    if (source == MSPTI_ACTIVITY_SOURCE_KIND_HOST)
    {
        rec.objectId.pt.processId = 3;
        rec.objectId.pt.threadId = 4;
    }
    else
    {
        rec.objectId.ds.deviceId = device;
        rec.objectId.ds.streamId = 5;
    }
    return rec;
}

template <typename Metric>
uint64_t DurationOf(const std::vector<Metric> &rows, uint32_t device)
{
    for (const auto &row : rows)
    {
        if (row.deviceId == device)
        {
            return row.duration;
        }
    }
    return 0;
}

}  // namespace

TEST(MetricActivityTest, DeviceAggregatesAndSend)
{
    MetricKernelProcess kernel;
    EXPECT_TRUE(kernel.AggregatedData().empty());
    auto k0 = MakeKernel(0, 100, 150);
    auto k1 = MakeKernel(0, 200, 280);
    auto k2 = MakeKernel(1, 10, 40);
    Consume(kernel, &k0);
    Consume(kernel, &k1);
    Consume(kernel, &k2);
    auto rows = kernel.AggregatedData();
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(DurationOf(rows, 0), 130u);
    EXPECT_EQ(DurationOf(rows, 1), 30u);
    EXPECT_NE(rows[0].seriesToJson().find("Kernel"), std::string::npos);
    auto k3 = MakeKernel(0, 1, 3);
    Consume(kernel, &k3);
    EXPECT_NO_THROW(kernel.SendProcessMessage());

    MetricHcclProcess hccl;
    auto h0 = MakeHccl(2, 1, 11);
    auto h1 = MakeHccl(3, 1, 6);
    Consume(hccl, &h0);
    Consume(hccl, &h1);
    auto hcclRows = hccl.AggregatedData();
    EXPECT_EQ(DurationOf(hcclRows, 2), 10u);
    EXPECT_EQ(DurationOf(hcclRows, 3), 5u);
    EXPECT_NE(hcclRows[0].seriesToJson().find("Hccl"), std::string::npos);
    auto h2 = MakeHccl(2, 1, 2);
    Consume(hccl, &h2);
    EXPECT_NO_THROW(hccl.SendProcessMessage());
    hccl.Clear();
    EXPECT_TRUE(hccl.AggregatedData().empty());

    MetricMemProcess mem;
    auto m0 = MakeMem(4, 5, 9);
    Consume(mem, &m0);
    auto memRows = mem.AggregatedData();
    ASSERT_EQ(memRows.size(), 1u);
    EXPECT_EQ(memRows[0].duration, 4u);
    EXPECT_NE(memRows[0].seriesToJson().find("Memory"), std::string::npos);
    auto m1 = MakeMem(4, 1, 2);
    Consume(mem, &m1);
    EXPECT_NO_THROW(mem.SendProcessMessage());

    MetricMemSetProcess memSet;
    auto s0 = MakeMemSet(5, 1, 8);
    Consume(memSet, &s0);
    auto setRows = memSet.AggregatedData();
    ASSERT_EQ(setRows.size(), 1u);
    EXPECT_EQ(setRows[0].duration, 7u);
    EXPECT_NE(setRows[0].seriesToJson().find("MemSet"), std::string::npos);
    auto s1 = MakeMemSet(5, 1, 2);
    Consume(memSet, &s1);
    EXPECT_NO_THROW(memSet.SendProcessMessage());
    memSet.Clear();

    MetricMemCpyProcess memCpy;
    auto c0 = MakeMemCpy(6, 2, 5);
    auto c1 = MakeMemCpy(6, 5, 9);
    Consume(memCpy, &c0);
    Consume(memCpy, &c1);
    auto cpyRows = memCpy.AggregatedData();
    ASSERT_EQ(cpyRows.size(), 1u);
    EXPECT_EQ(cpyRows[0].duration, 7u);
    EXPECT_NE(cpyRows[0].seriesToJson().find("MemCpy"), std::string::npos);
    auto c2 = MakeMemCpy(6, 1, 2);
    Consume(memCpy, &c2);
    EXPECT_NO_THROW(memCpy.SendProcessMessage());

    MetricCommunicationProcess comm;
    auto d0 = MakeComm(7, 10, 30);
    auto d1 = MakeComm(8, 10, 12);
    Consume(comm, &d0);
    Consume(comm, &d1);
    auto commRows = comm.AggregatedData();
    EXPECT_EQ(DurationOf(commRows, 7), 20u);
    EXPECT_EQ(DurationOf(commRows, 8), 2u);
    EXPECT_NE(commRows[0].seriesToJson().find("Communication"), std::string::npos);
    auto d2 = MakeComm(7, 1, 2);
    Consume(comm, &d2);
    EXPECT_NO_THROW(comm.SendProcessMessage());
}

TEST(MetricActivityTest, ApiAggregateAndClear)
{
    MetricApiProcess api("API");
    EXPECT_TRUE(api.AggregatedData().empty());
    auto a0 = MakeApi(10, 25);
    auto a1 = MakeApi(30, 50);
    Consume(api, &a0);
    Consume(api, &a1);
    auto rows = api.AggregatedData();
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].duration, 35u);
    EXPECT_EQ(rows[0].kind, "API");
    EXPECT_NE(rows[0].seriesToJson().find("API"), std::string::npos);
    auto again = MakeApi(1, 4);
    Consume(api, &again);
    EXPECT_NO_THROW(api.SendProcessMessage());
    api.Clear();

    MetricApiProcess acl("AclAPI");
    auto one = MakeApi(1, 2);
    Consume(acl, &one);
    acl.Clear();
    EXPECT_TRUE(acl.AggregatedData().empty());
}

TEST(MetricActivityTest, MarkRangeCompleteAndIncomplete)
{
    MetricMarkProcess mark;
    auto lone = MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_END, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 1, 10, 0);
    Consume(mark, &lone);
    EXPECT_TRUE(mark.AggregatedData().empty());

    auto hostStart =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_START_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 9, 1000, 0);
    auto devStart =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_START_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_DEVICE, 9, 1100, 3);
    auto hostEnd = MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_END_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 9, 2000, 0);
    auto devEnd = MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_END_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_DEVICE, 9, 1800, 3);
    Consume(mark, &hostStart);
    Consume(mark, &devStart);
    Consume(mark, &hostEnd);
    Consume(mark, &devEnd);
    auto rows = mark.AggregatedData();
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].name, "range");
    EXPECT_EQ(rows[0].domain, "domain");
    EXPECT_EQ(rows[0].duration, 1000u);
    EXPECT_EQ(rows[1].duration, 700u);
    EXPECT_NE(rows[0].seriesToJson().find("Marker"), std::string::npos);
    auto againStart =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_START_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 2, 1, 0);
    auto againDevStart =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_START_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_DEVICE, 2, 2, 1);
    auto againHostEnd =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_END_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 2, 5, 0);
    auto againDevEnd =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_END_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_DEVICE, 2, 4, 1);
    Consume(mark, &againStart);
    Consume(mark, &againDevStart);
    Consume(mark, &againHostEnd);
    Consume(mark, &againDevEnd);
    EXPECT_NO_THROW(mark.SendProcessMessage());

    MetricMarkProcess fallback;
    // Keep a lower incomplete id so AggregatedData does not erase the only map node.
    auto keep = MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_END, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 1, 10, 0);
    Consume(fallback, &keep);
    for (int i = 0; i < 4; ++i)
    {
        auto rec = MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_END_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_DEVICE, 4, 10, 1);
        Consume(fallback, &rec);
    }
    auto plain = fallback.AggregatedData();
    ASSERT_EQ(plain.size(), 2u);
    EXPECT_EQ(plain[0].domain, "default");
    fallback.Clear();
    EXPECT_TRUE(fallback.AggregatedData().empty());
}

TEST(MetricManagerTest, SwitchConsumeAndReport)
{
    MetricManager manager;
    msptiActivity invalid{MSPTI_ACTIVITY_KIND_INVALID};
    EXPECT_EQ(manager.ConsumeMsptiData(&invalid), ErrCode::PARAM);
    msptiActivity overflow{MSPTI_ACTIVITY_KIND_COUNT};
    EXPECT_EQ(manager.ConsumeMsptiData(&overflow), ErrCode::PARAM);

    auto kernel = MakeKernel(0, 1, 4);
    EXPECT_EQ(manager.ConsumeMsptiData(reinterpret_cast<msptiActivity *>(&kernel)), ErrCode::PERMISSION);
    manager.EnableKindSwitch(MSPTI_ACTIVITY_KIND_KERNEL, true);
    manager.EnableKindSwitch(MSPTI_ACTIVITY_KIND_API, true);
    EXPECT_EQ(manager.ConsumeMsptiData(reinterpret_cast<msptiActivity *>(&kernel)), ErrCode::SUC);
    auto api = MakeApi(2, 6);
    api.kind = MSPTI_ACTIVITY_KIND_API;
    EXPECT_EQ(manager.ConsumeMsptiData(reinterpret_cast<msptiActivity *>(&api)), ErrCode::SUC);

    EXPECT_NO_THROW(manager.SetReportInterval(1));
    EXPECT_NO_THROW(manager.SetReportInterval(2));
    EXPECT_NO_THROW(manager.SetReportInterval(2));
    EXPECT_NO_THROW(manager.ExecuteTask());
    EXPECT_NO_THROW(manager.RunPostTask());
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
