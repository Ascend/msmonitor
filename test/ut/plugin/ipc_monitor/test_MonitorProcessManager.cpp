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

#include "plugin/ipc_monitor/monitor/Monitor.h"
#include "plugin/ipc_monitor/monitor/MonitorProcessManager.h"

using dynolog_npu::ipc_monitor::ErrCode;
using dynolog_npu::ipc_monitor::monitor::Monitor;
using dynolog_npu::ipc_monitor::monitor::MonitorProcessManager;

namespace
{

constexpr uint64_t kEnd = 4000000000000000000ULL;

void Feed(MonitorProcessManager &mgr, void *record) { mgr.ConsumeMsptiData(static_cast<msptiActivity *>(record)); }

msptiActivityApi MakeApi(msptiActivityKind kind, uint64_t end)
{
    msptiActivityApi rec{};
    rec.kind = kind;
    rec.start = end - 4;
    rec.end = end;
    rec.pt.processId = 3;
    rec.pt.threadId = 4;
    rec.correlationId = 5;
    rec.name = "launch";
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
    rec.name = "mark";
    rec.domain = "dom";
    if (source == MSPTI_ACTIVITY_SOURCE_KIND_HOST)
    {
        rec.objectId.pt.processId = 6;
        rec.objectId.pt.threadId = 7;
    }
    else
    {
        rec.objectId.ds.deviceId = device;
        rec.objectId.ds.streamId = 8;
    }
    return rec;
}

}  // namespace

TEST(MonitorProcessManagerTest, NullEarlyAndUnsupported)
{
    MonitorProcessManager mgr;
    EXPECT_EQ(mgr.ConsumeMsptiData(nullptr), ErrCode::VALUE);
    mgr.RunPreTask();
    auto early = MakeApi(MSPTI_ACTIVITY_KIND_API, 1);
    Feed(mgr, &early);
    msptiActivity memory{MSPTI_ACTIVITY_KIND_MEMORY};
    EXPECT_EQ(mgr.ConsumeMsptiData(&memory), ErrCode::SUC);
    auto dropped = MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_INSTANTANEOUS, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 1, 1, 0);
    Feed(mgr, &dropped);
    mgr.RunPostTask();
    EXPECT_TRUE(Monitor::GetInstance()->GetAPIData().empty());
    EXPECT_TRUE(Monitor::GetInstance()->GetMarkerData().empty());
}

TEST(MonitorProcessManagerTest, ReportsApiKernelCommunicationAndMarkers)
{
    MonitorProcessManager mgr;
    mgr.RunPreTask();

    auto api = MakeApi(MSPTI_ACTIVITY_KIND_API, kEnd);
    auto acl = MakeApi(MSPTI_ACTIVITY_KIND_ACL_API, kEnd);
    auto node = MakeApi(MSPTI_ACTIVITY_KIND_NODE_API, kEnd);
    auto runtime = MakeApi(MSPTI_ACTIVITY_KIND_RUNTIME_API, kEnd);
    Feed(mgr, &api);
    Feed(mgr, &acl);
    Feed(mgr, &node);
    Feed(mgr, &runtime);

    msptiActivityKernel kernel{};
    kernel.kind = MSPTI_ACTIVITY_KIND_KERNEL;
    kernel.start = kEnd - 10;
    kernel.end = kEnd;
    kernel.ds.deviceId = 1;
    kernel.ds.streamId = 2;
    kernel.correlationId = 9;
    kernel.type = "vec";
    kernel.name = "add";
    msptiActivityKernel kernelEarly = kernel;
    kernelEarly.end = 1;
    Feed(mgr, &kernelEarly);
    Feed(mgr, &kernel);

    msptiActivityCommunication comm{};
    comm.kind = MSPTI_ACTIVITY_KIND_COMMUNICATION;
    comm.start = kEnd - 8;
    comm.end = kEnd;
    comm.ds.deviceId = 1;
    comm.ds.streamId = 2;
    comm.count = 4;
    comm.dataType = MSPTI_ACTIVITY_COMMUNICATION_INT8;
    comm.name = "allreduce";
    comm.commName = "g";
    comm.algType = "ring";
    comm.correlationId = 10;
    msptiActivityCommunication commEarly = comm;
    commEarly.end = 1;
    Feed(mgr, &commEarly);
    Feed(mgr, &comm);

    auto hostInstant =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_INSTANTANEOUS, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 2, kEnd, 0);
    auto hostWithDev =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_INSTANTANEOUS_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 3, kEnd, 0);
    auto devWithHost =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_INSTANTANEOUS_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_DEVICE, 3, kEnd, 1);
    auto devAlone = MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_INSTANTANEOUS_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_DEVICE,
                               30, kEnd, 1);
    auto hostStart = MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_START, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 4, kEnd, 0);
    auto hostEnd = MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_END, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 4, kEnd + 3, 0);
    auto hostEndMiss = MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_END, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 44, kEnd, 0);
    auto hostStartDev =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_START_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 5, kEnd, 0);
    auto hostEndDev =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_END_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 5, kEnd + 5, 0);
    auto devStart =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_START_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_DEVICE, 5, kEnd, 2);
    auto devEnd =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_END_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_DEVICE, 5, kEnd + 6, 2);
    auto devEndMiss =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_END_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_DEVICE, 55, kEnd, 2);
    Feed(mgr, &hostInstant);
    Feed(mgr, &hostWithDev);
    Feed(mgr, &devWithHost);
    Feed(mgr, &devAlone);
    Feed(mgr, &hostStart);
    Feed(mgr, &hostEnd);
    Feed(mgr, &hostEndMiss);
    Feed(mgr, &hostStartDev);
    Feed(mgr, &hostEndDev);
    Feed(mgr, &devStart);
    Feed(mgr, &devEnd);
    Feed(mgr, &devEndMiss);
    mgr.RunPostTask();

    auto *monitor = Monitor::GetInstance();
    EXPECT_EQ(monitor->GetAPIData().size(), 1u);
    EXPECT_EQ(monitor->GetAclApiData().size(), 1u);
    EXPECT_EQ(monitor->GetNodeApiData().size(), 1u);
    EXPECT_EQ(monitor->GetRuntimeApiData().size(), 1u);
    EXPECT_EQ(monitor->GetKernelData().size(), 1u);
    EXPECT_EQ(monitor->GetCommunicationData().size(), 1u);
    EXPECT_EQ(monitor->GetMarkerData().size(), 7u);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
