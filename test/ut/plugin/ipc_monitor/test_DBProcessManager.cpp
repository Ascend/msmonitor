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

#include <filesystem>
#include <mockcpp/mockcpp.hpp>
#include <string>
#include <vector>

#include "plugin/ipc_monitor/db/DBProcessManager.h"
#include "plugin/ipc_monitor/mspti_monitor/MsptiMonitor.h"
#include "plugin/ipc_monitor/utils/utils.h"

using dynolog_npu::ipc_monitor::ErrCode;
using dynolog_npu::ipc_monitor::GetRankId;
using dynolog_npu::ipc_monitor::MsptiMonitor;
using dynolog_npu::ipc_monitor::db::DBProcessManager;
using dynolog_npu::ipc_monitor::db::DBRunner;

namespace
{

constexpr uint64_t kEnd = 4000000000000000000ULL;

class TempDir
{
   public:
    TempDir()
    {
        std::string tmpl = "/tmp/msmonitor_dbpm_XXXXXX";
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

msptiActivityApi MakeApi(msptiActivityKind kind, uint64_t end, const char *name)
{
    msptiActivityApi rec{};
    rec.kind = kind;
    rec.start = end - 10;
    rec.end = end;
    rec.pt.processId = 4;
    rec.pt.threadId = 5;
    rec.correlationId = 6;
    rec.name = name;
    return rec;
}

msptiActivityCommunication MakeComm(uint32_t device, const char *group, uint64_t end)
{
    msptiActivityCommunication rec{};
    rec.kind = MSPTI_ACTIVITY_KIND_COMMUNICATION;
    rec.start = end - 20;
    rec.end = end;
    rec.ds.deviceId = device;
    rec.ds.streamId = 1;
    rec.dataType = MSPTI_ACTIVITY_COMMUNICATION_FP16;
    rec.count = 8;
    rec.name = "allreduce";
    rec.commName = group;
    rec.algType = "ring";
    rec.correlationId = 9;
    return rec;
}

msptiActivityKernel MakeKernel(uint32_t device, uint64_t end)
{
    msptiActivityKernel rec{};
    rec.kind = MSPTI_ACTIVITY_KIND_KERNEL;
    rec.start = end - 30;
    rec.end = end;
    rec.ds.deviceId = device;
    rec.ds.streamId = 2;
    rec.correlationId = 11;
    rec.type = "mix";
    rec.name = "matmul";
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
        rec.objectId.pt.processId = 1;
        rec.objectId.pt.threadId = 2;
    }
    else
    {
        rec.objectId.ds.deviceId = device;
        rec.objectId.ds.streamId = 3;
    }
    return rec;
}

void Feed(DBProcessManager &mgr, void *record) { mgr.ConsumeMsptiData(static_cast<msptiActivity *>(record)); }

bool HasDb(const std::string &dir)
{
    for (const auto &ent : std::filesystem::directory_iterator(dir))
    {
        if (ent.path().extension() == ".db")
        {
            return true;
        }
    }
    return false;
}

}  // namespace

class DBProcessManagerTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        ASSERT_FALSE(dir_.Path().empty());
        MOCKER_CPP(&GetRankId).stubs().will(returnValue(-1));
        MsptiMonitor::GetInstance()->SetClusterConfigData({});
    }

    void TearDown() override
    {
        MsptiMonitor::GetInstance()->SetClusterConfigData({});
        GlobalMockObject::verify();
    }

    TempDir dir_;
};

TEST_F(DBProcessManagerTest, NullAndUnsupported)
{
    DBProcessManager mgr(dir_.Path());
    EXPECT_EQ(mgr.ConsumeMsptiData(nullptr), ErrCode::VALUE);
    msptiActivity memory{MSPTI_ACTIVITY_KIND_MEMORY};
    EXPECT_EQ(mgr.ConsumeMsptiData(&memory), ErrCode::SUC);
    mgr.SetReportInterval(0);
    mgr.SetReportInterval(3);
    mgr.ExecuteTask();
    mgr.RunPostTask();
}

TEST_F(DBProcessManagerTest, CreateDbFailureDoesNotThrow)
{
    DBProcessManager mgr("/proc/msmonitor_ut_not_a_dir");
    mgr.RunPreTask();
    auto api = MakeApi(MSPTI_ACTIVITY_KIND_API, kEnd, "launch");
    Feed(mgr, &api);
    EXPECT_NO_THROW(mgr.ExecuteTask());
    EXPECT_NO_THROW(mgr.RunPostTask());
}

TEST_F(DBProcessManagerTest, ConsumeSaveAndPost)
{
    DBProcessManager light(dir_.Path());
    light.RunPreTask();
    auto early = MakeApi(MSPTI_ACTIVITY_KIND_API, 1, "early");
    Feed(light, &early);
    auto api = MakeApi(MSPTI_ACTIVITY_KIND_API, kEnd, "launch");
    Feed(light, &api);
    light.ExecuteTask();
    light.RunPostTask();
    EXPECT_TRUE(HasDb(dir_.Path()));

    MsptiMonitor::GetInstance()->SetClusterConfigData({{"parallel_group_info", ""}});
    DBProcessManager emptyGroup(dir_.Path());
    emptyGroup.RunPreTask();
    auto acl = MakeApi(MSPTI_ACTIVITY_KIND_ACL_API, kEnd, "acl");
    Feed(emptyGroup, &acl);
    emptyGroup.RunPostTask();

    MsptiMonitor::GetInstance()->SetClusterConfigData({{"parallel_group_info", "{\"tp\":[0,1]}"}});
    DBProcessManager mgr(dir_.Path());
    mgr.RunPreTask();
    auto node = MakeApi(MSPTI_ACTIVITY_KIND_NODE_API, kEnd, "node");
    auto runtime = MakeApi(MSPTI_ACTIVITY_KIND_RUNTIME_API, kEnd, "rt");
    auto droppedComm = MakeComm(0, "g", 1);
    auto comm0 = MakeComm(0, "g", kEnd);
    auto comm1 = MakeComm(0, "g", kEnd);
    auto comm2 = MakeComm(1, "h", kEnd);
    auto droppedKernel = MakeKernel(1, 1);
    auto kernel0 = MakeKernel(1, kEnd);
    auto kernel1 = MakeKernel(2, kEnd);
    Feed(mgr, &node);
    Feed(mgr, &runtime);
    Feed(mgr, &droppedComm);
    Feed(mgr, &comm0);
    Feed(mgr, &comm1);
    Feed(mgr, &comm2);
    Feed(mgr, &droppedKernel);
    Feed(mgr, &kernel0);
    Feed(mgr, &kernel1);

    auto droppedMark = MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_INSTANTANEOUS, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 1, 1, 0);
    auto hostInstant =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_INSTANTANEOUS, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 2, kEnd, 0);
    auto hostInstantDev =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_INSTANTANEOUS_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 3, kEnd, 0);
    auto hostStart = MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_START, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 4, kEnd, 0);
    auto hostEnd = MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_END, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 4, kEnd + 5, 0);
    auto hostEndMiss = MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_END, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 40, kEnd, 0);
    auto hostStartDev =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_START_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 5, kEnd, 0);
    auto hostEndDev =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_END_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_HOST, 5, kEnd + 8, 0);
    auto devInstant =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_INSTANTANEOUS_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_DEVICE, 3, kEnd, 1);
    auto devStart =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_START_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_DEVICE, 5, kEnd, 1);
    auto devEnd =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_END_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_DEVICE, 5, kEnd + 9, 1);
    auto devEndMiss =
        MakeMarker(MSPTI_ACTIVITY_FLAG_MARKER_END_WITH_DEVICE, MSPTI_ACTIVITY_SOURCE_KIND_DEVICE, 50, kEnd, 1);
    Feed(mgr, &droppedMark);
    Feed(mgr, &hostInstant);
    Feed(mgr, &hostInstantDev);
    Feed(mgr, &hostStart);
    Feed(mgr, &hostEnd);
    Feed(mgr, &hostEndMiss);
    Feed(mgr, &hostStartDev);
    Feed(mgr, &hostEndDev);
    Feed(mgr, &devInstant);
    Feed(mgr, &devStart);
    Feed(mgr, &devEnd);
    Feed(mgr, &devEndMiss);

    mgr.SetReportInterval(3);
    mgr.ExecuteTask();
    mgr.Run();
    mgr.SetReportInterval(6);
    mgr.Stop();
    EXPECT_TRUE(HasDb(dir_.Path()));
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
