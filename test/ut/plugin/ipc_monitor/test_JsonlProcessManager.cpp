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
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <mockcpp/mockcpp.hpp>
#include <string>
#include <vector>

#include "plugin/ipc_monitor/jsonl/JsonlProcessManager.h"
#include "plugin/ipc_monitor/mspti_monitor/MsptiMonitor.h"
#include "plugin/ipc_monitor/utils/utils.h"

using dynolog_npu::ipc_monitor::ErrCode;
using dynolog_npu::ipc_monitor::GetRankId;
using dynolog_npu::ipc_monitor::MsptiMonitor;
using dynolog_npu::ipc_monitor::jsonl::JsonlProcessManager;

namespace
{

constexpr uint64_t kEnd = 4000000000000000000ULL;

class TempDir
{
   public:
    TempDir()
    {
        std::string tmpl = "/tmp/msmonitor_jpm_XXXXXX";
        buf_.assign(tmpl.begin(), tmpl.end());
        buf_.push_back('\0');
        char *dir = mkdtemp(buf_.data());
        if (dir != nullptr)
        {
            path_ = dir;
        }
    }

    ~TempDir()
    {
        if (!path_.empty())
        {
            std::error_code ec;
            std::filesystem::remove_all(path_, ec);
        }
    }

    const std::string &Path() const { return path_; }

   private:
    std::vector<char> buf_;
    std::string path_;
};

void Feed(JsonlProcessManager &mgr, void *record) { mgr.ConsumeMsptiData(static_cast<msptiActivity *>(record)); }

msptiActivityApi MakeApi(msptiActivityKind kind, uint64_t end)
{
    msptiActivityApi rec{};
    rec.kind = kind;
    rec.start = end - 5;
    rec.end = end;
    rec.pt.processId = 1;
    rec.pt.threadId = 2;
    rec.correlationId = 3;
    rec.name = "api";
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
        rec.objectId.pt.processId = 8;
        rec.objectId.pt.threadId = 9;
    }
    else
    {
        rec.objectId.ds.deviceId = device;
        rec.objectId.ds.streamId = 1;
    }
    return rec;
}

}  // namespace

class JsonlProcessManagerTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        ASSERT_FALSE(dir_.Path().empty());
        MOCKER_CPP(&GetRankId).stubs().will(returnValue(-1));
        MsptiMonitor::GetInstance()->SetClusterConfigData({});
        unsetenv("MSMONITOR_JSONL_BUFFER_CAPACITY");
        unsetenv("MSMONITOR_JSONL_MAX_DUMP_INTERVAL");
    }

    void TearDown() override
    {
        unsetenv("MSMONITOR_JSONL_BUFFER_CAPACITY");
        unsetenv("MSMONITOR_JSONL_MAX_DUMP_INTERVAL");
        MsptiMonitor::GetInstance()->SetClusterConfigData({});
        GlobalMockObject::verify();
    }

    TempDir dir_;
};

TEST_F(JsonlProcessManagerTest, NullUnsupportedAndEnvCapacity)
{
    JsonlProcessManager mgr(dir_.Path(), "50", "1");
    EXPECT_EQ(mgr.ConsumeMsptiData(nullptr), ErrCode::VALUE);
    msptiActivity memory{MSPTI_ACTIVITY_KIND_MEMORY};
    EXPECT_EQ(mgr.ConsumeMsptiData(&memory), ErrCode::SUC);
    mgr.SetReportInterval(0);
    mgr.SetReportInterval(2);
    mgr.ExecuteTask();
    mgr.RunPreTask();
    mgr.RunPostTask();

    ASSERT_EQ(setenv("MSMONITOR_JSONL_BUFFER_CAPACITY", "4096", 1), 0);
    ASSERT_EQ(setenv("MSMONITOR_JSONL_MAX_DUMP_INTERVAL", "500", 1), 0);
    JsonlProcessManager clamp(dir_.Path(), "", "");
    clamp.RunPreTask();
    clamp.RunPostTask();

    ASSERT_EQ(setenv("MSMONITOR_JSONL_BUFFER_CAPACITY", "4194304", 1), 0);
    ASSERT_EQ(setenv("MSMONITOR_JSONL_MAX_DUMP_INTERVAL", "2000", 1), 0);
    JsonlProcessManager high(dir_.Path(), "600000", "3");
    high.RunPreTask();
    high.RunPostTask();

    ASSERT_EQ(setenv("MSMONITOR_JSONL_BUFFER_CAPACITY", "1000", 1), 0);
    ASSERT_EQ(setenv("MSMONITOR_JSONL_MAX_DUMP_INTERVAL", "zz", 1), 0);
    JsonlProcessManager bad(dir_.Path(), "abc", "0");
    bad.RunPreTask();
    bad.RunPostTask();
}

TEST_F(JsonlProcessManagerTest, RecordsFlushedWithAndWithoutGroup)
{
    JsonlProcessManager apiOnly(dir_.Path(), "200", "-1");
    apiOnly.RunPreTask();
    auto early = MakeApi(MSPTI_ACTIVITY_KIND_API, 1);
    auto api = MakeApi(MSPTI_ACTIVITY_KIND_API, kEnd);
    Feed(apiOnly, &early);
    Feed(apiOnly, &api);
    apiOnly.ExecuteTask();
    apiOnly.RunPostTask();

    MsptiMonitor::GetInstance()->SetClusterConfigData({{"parallel_group_info", ""}});
    JsonlProcessManager emptyGroup(dir_.Path(), "200", "-1");
    emptyGroup.RunPreTask();
    auto acl = MakeApi(MSPTI_ACTIVITY_KIND_ACL_API, kEnd);
    Feed(emptyGroup, &acl);
    emptyGroup.RunPostTask();

    MsptiMonitor::GetInstance()->SetClusterConfigData({{"parallel_group_info", "group-info"}});
    JsonlProcessManager mgr(dir_.Path(), "200", "4");
    mgr.RunPreTask();
    auto node = MakeApi(MSPTI_ACTIVITY_KIND_NODE_API, kEnd);
    auto runtime = MakeApi(MSPTI_ACTIVITY_KIND_RUNTIME_API, kEnd);
    Feed(mgr, &node);
    Feed(mgr, &runtime);

    msptiActivityCommunication comm{};
    comm.kind = MSPTI_ACTIVITY_KIND_COMMUNICATION;
    comm.start = kEnd - 15;
    comm.end = kEnd;
    comm.ds.deviceId = 1;
    comm.ds.streamId = 2;
    comm.dataType = MSPTI_ACTIVITY_COMMUNICATION_FP32;
    comm.count = 4;
    comm.name = "allgather";
    comm.commName = "g";
    comm.algType = "ring";
    comm.correlationId = 7;
    msptiActivityCommunication commEarly = comm;
    commEarly.end = 1;
    Feed(mgr, &commEarly);
    Feed(mgr, &comm);

    msptiActivityKernel kernel{};
    kernel.kind = MSPTI_ACTIVITY_KIND_KERNEL;
    kernel.start = kEnd - 12;
    kernel.end = kEnd;
    kernel.ds.deviceId = 2;
    kernel.ds.streamId = 3;
    kernel.type = "vec";
    kernel.name = "add";
    kernel.correlationId = 8;
    msptiActivityKernel kernelEarly = kernel;
    kernelEarly.end = 1;
    Feed(mgr, &kernelEarly);
    Feed(mgr, &kernel);

    auto dropped = MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_INSTANTANEOUS, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 1, 1, 0);
    auto hostInstant =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_INSTANTANEOUS, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 2, kEnd, 0);
    auto hostWithDev =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_INSTANTANEOUS_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 3, kEnd, 0);
    auto devWithHost =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_INSTANTANEOUS_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_DEVICE, 3, kEnd, 1);
    auto devAlone = MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_INSTANTANEOUS_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_DEVICE,
                               30, kEnd, 1);
    auto hostStart = MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_START, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 4, kEnd, 0);
    auto hostEnd = MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_END, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 4, kEnd + 4, 0);
    auto hostEndMiss = MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_END, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 41, kEnd, 0);
    auto hostStartDev =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_START_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 5, kEnd, 0);
    auto hostEndDev =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_END_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 5, kEnd + 6, 0);
    auto devStart =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_START_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_DEVICE, 5, kEnd, 2);
    auto devEnd =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_END_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_DEVICE, 5, kEnd + 7, 2);
    auto devEndMiss =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_END_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_DEVICE, 55, kEnd, 2);
    Feed(mgr, &dropped);
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

    JsonlProcessManager timed(dir_.Path(), "200", "-1");
    timed.Run();
    timed.SetReportInterval(4);
    timed.Stop();

    bool found = false;
    for (const auto &ent : std::filesystem::directory_iterator(dir_.Path()))
    {
        if (ent.path().extension() == ".jsonl")
        {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
