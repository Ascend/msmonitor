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
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "plugin/ipc_monitor/mspti_monitor/MsptiMonitor.h"

using namespace dynolog_npu::ipc_monitor;

namespace
{

class TempDir
{
   public:
    TempDir()
    {
        std::string tmpl = "/tmp/msmonitor_msptimon_XXXXXX";
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

// 替换真实数据处理器：记录开关/上报间隔，避免依赖 DB/Jsonl 落盘
class FakeProcessor : public MsptiDataProcessBase
{
   public:
    FakeProcessor() : MsptiDataProcessBase("FakeProcessor") {}

    ErrCode ConsumeMsptiData(msptiActivity *record) override
    {
        consumeCount++;
        return ErrCode::SUC;
    }

    void EnableKindSwitch(msptiActivityKind kind, bool flag) override { kindSwitches[kind] = flag; }

    void SetReportInterval(uint32_t interval) override { reportInterval = interval; }

    std::atomic<int> consumeCount{0};
    std::unordered_map<msptiActivityKind, bool> kindSwitches;
    std::atomic<uint32_t> reportInterval{0};
};

msptiFilterItems MakeFilters(msptiActivityKind kind, std::unordered_set<std::string> ops)
{
    msptiFilterItems filters;
    filters[kind] = std::move(ops);
    return filters;
}

// 具体活动结构体指针按 mspti.h 布局对齐转换（与生产 BufferConsume 的转发方式一致）
template <typename T>
msptiActivity *ToActivity(T *record)
{
    return ReinterpretConvert<msptiActivity *>(record);
}

}  // namespace

TEST(MsptiMonitorTest, ShouldKeepRecordFilterMatrix)
{
    auto *monitor = MsptiMonitor::GetInstance();

    msptiActivityKernel kernel{};
    kernel.kind = MSPTI_ACTIVITY_KIND_KERNEL;
    kernel.name = "add_op";

    EXPECT_FALSE(monitor->ShouldKeepRecord(nullptr));
    EXPECT_TRUE(monitor->ShouldKeepRecord(ToActivity(&kernel)));  // 无过滤：全保留

    monitor->SetFilterItems(MakeFilters(MSPTI_ACTIVITY_KIND_KERNEL, {"add"}));
    EXPECT_TRUE(monitor->ShouldKeepRecord(ToActivity(&kernel)));  // 名称命中保留
    kernel.name = "sub_op";
    EXPECT_FALSE(monitor->ShouldKeepRecord(ToActivity(&kernel)));  // 未命中丢弃
    kernel.name = "";
    EXPECT_FALSE(monitor->ShouldKeepRecord(ToActivity(&kernel)));  // 空名称丢弃

    // 白名单 kind（Memory/Memset/MemCpy）不参与名称过滤
    msptiActivityMemory mem{};
    mem.kind = MSPTI_ACTIVITY_KIND_MEMORY;
    msptiActivityMemset memsetRec{};
    memsetRec.kind = MSPTI_ACTIVITY_KIND_MEMSET;
    msptiActivityMemcpy memcpyRec{};
    memcpyRec.kind = MSPTI_ACTIVITY_KIND_MEMCPY;
    EXPECT_TRUE(monitor->ShouldKeepRecord(ToActivity(&mem)));
    EXPECT_TRUE(monitor->ShouldKeepRecord(ToActivity(&memsetRec)));
    EXPECT_TRUE(monitor->ShouldKeepRecord(ToActivity(&memcpyRec)));

    // API kind 不在过滤表中：丢弃；命中后按名称匹配
    msptiActivityApi api{};
    api.kind = MSPTI_ACTIVITY_KIND_API;
    api.name = "acl_launch";
    EXPECT_FALSE(monitor->ShouldKeepRecord(ToActivity(&api)));
    monitor->SetFilterItems(MakeFilters(MSPTI_ACTIVITY_KIND_API, {"launch"}));
    EXPECT_TRUE(monitor->ShouldKeepRecord(ToActivity(&api)));

    // Marker：host 来源且名称有效 flag 才按名过滤
    msptiActivityMarker marker{};
    marker.kind = MSPTI_ACTIVITY_KIND_MARKER;
    marker.name = "mark_step";
    marker.sourceKind = MSPTI_ACTIVITY_SOURCE_KIND_HOST;
    marker.flag = MSPTI_ACTIVITY_FLAG_MARKER_INSTANTANEOUS;
    monitor->SetFilterItems(MakeFilters(MSPTI_ACTIVITY_KIND_MARKER, {"mark"}));
    EXPECT_TRUE(monitor->ShouldKeepRecord(ToActivity(&marker)));
    marker.name = "other_step";
    EXPECT_FALSE(monitor->ShouldKeepRecord(ToActivity(&marker)));
    marker.flag = MSPTI_ACTIVITY_FLAG_MARKER_END;  // 结束 flag 无有效名称：直接保留
    EXPECT_TRUE(monitor->ShouldKeepRecord(ToActivity(&marker)));
    marker.flag = MSPTI_ACTIVITY_FLAG_MARKER_INSTANTANEOUS;
    marker.sourceKind = MSPTI_ACTIVITY_SOURCE_KIND_DEVICE;  // device 来源不按名过滤
    EXPECT_TRUE(monitor->ShouldKeepRecord(ToActivity(&marker)));

    // Communication 按名称匹配
    msptiActivityCommunication comm{};
    comm.kind = MSPTI_ACTIVITY_KIND_COMMUNICATION;
    comm.name = "allreduce";
    monitor->SetFilterItems(MakeFilters(MSPTI_ACTIVITY_KIND_COMMUNICATION, {"all"}));
    EXPECT_TRUE(monitor->ShouldKeepRecord(ToActivity(&comm)));
    comm.name = "bcast";
    EXPECT_FALSE(monitor->ShouldKeepRecord(ToActivity(&comm)));

    // 未在过滤表且非白名单的 kind：丢弃
    msptiActivityHccl hccl{};
    hccl.kind = MSPTI_ACTIVITY_KIND_HCCL;
    EXPECT_FALSE(monitor->ShouldKeepRecord(ToActivity(&hccl)));

    monitor->SetFilterItems({});  // 清理过滤状态，避免影响后续用例
}

TEST(MsptiMonitorTest, LifecycleEnableDisableAndStop)
{
    auto *monitor = MsptiMonitor::GetInstance();
    EXPECT_FALSE(monitor->IsStarted());
    EXPECT_NO_THROW(monitor->Stop());  // 未启动时 Stop 幂等
    EXPECT_TRUE(monitor->IsMetricMode());

    auto proc = std::make_shared<FakeProcessor>();
    monitor->SetDataProcessor(proc);
    monitor->Start();
    EXPECT_TRUE(monitor->IsStarted());
    EXPECT_EQ(proc->reportInterval.load(), 0u);  // Start 应用当前 flushInterval（默认 0）

    monitor->SetFlushInterval(3);
    EXPECT_EQ(proc->reportInterval.load(), 3u);

    monitor->EnableActivity(MSPTI_ACTIVITY_KIND_KERNEL);
    EXPECT_EQ(monitor->GetEnabledActivities().size(), 1u);
    EXPECT_EQ(proc->kindSwitches[MSPTI_ACTIVITY_KIND_KERNEL], true);

    // 越界 kind 不生效，也不向处理器下发开关
    monitor->EnableActivity(MSPTI_ACTIVITY_KIND_INVALID);
    monitor->EnableActivity(MSPTI_ACTIVITY_KIND_COUNT);
    EXPECT_EQ(monitor->GetEnabledActivities().size(), 1u);
    EXPECT_EQ(proc->kindSwitches.size(), 1u);

    monitor->DisableActivity(MSPTI_ACTIVITY_KIND_KERNEL);
    EXPECT_TRUE(monitor->GetEnabledActivities().empty());
    EXPECT_EQ(proc->kindSwitches[MSPTI_ACTIVITY_KIND_KERNEL], false);
    monitor->DisableActivity(MSPTI_ACTIVITY_KIND_API);  // 未使能 kind：仅下发关闭开关
    EXPECT_EQ(proc->kindSwitches[MSPTI_ACTIVITY_KIND_API], false);

    monitor->SetClusterConfigData({{"rank", "1"}});
    EXPECT_EQ(monitor->GetClusterConfigData().at("rank"), "1");

    monitor->Stop();
    EXPECT_FALSE(monitor->IsStarted());
    EXPECT_TRUE(monitor->GetEnabledActivities().empty());  // Run 退出时清理使能集合
}

TEST(MsptiMonitorTest, StartIdempotentAndRestart)
{
    auto *monitor = MsptiMonitor::GetInstance();
    auto proc = std::make_shared<FakeProcessor>();
    monitor->SetDataProcessor(proc);

    monitor->Start();
    EXPECT_TRUE(monitor->IsStarted());
    monitor->Start();  // 重复启动：直接返回
    EXPECT_TRUE(monitor->IsStarted());

    monitor->Stop();
    monitor->Stop();  // 重复停止：告警返回不崩溃
    EXPECT_FALSE(monitor->IsStarted());

    monitor->Start();  // 停止后可重启
    EXPECT_TRUE(monitor->IsStarted());
    monitor->Stop();
    EXPECT_FALSE(monitor->IsStarted());
}

TEST(MsptiMonitorTest, SavePathCheckAndExportType)
{
    auto *monitor = MsptiMonitor::GetInstance();
    TempDir tmp;

    EXPECT_FALSE(monitor->CheckAndSetSavePath(""));  // 空路径拒绝
    EXPECT_TRUE(monitor->CheckAndSetSavePath(tmp.Path() + "/save"));
    EXPECT_FALSE(monitor->IsMetricMode());  // 设置保存路径后进入落盘模式

    std::string file = tmp.Path() + "/plain";
    {
        std::ofstream ofs(file, std::ios::out);
        ofs << "x";
    }
    EXPECT_FALSE(monitor->CheckAndSetSavePath(file));  // 已存在文件不是合法目录

    // 非法导出类型：处理器创建失败，启动被拒绝
    monitor->SetExportType("BadType");
    monitor->Start();
    EXPECT_FALSE(monitor->IsStarted());

    // 注入处理器后可正常启停，Uninit 清理保存路径恢复 metric 模式
    auto proc = std::make_shared<FakeProcessor>();
    monitor->SetDataProcessor(proc);
    monitor->Start();
    EXPECT_TRUE(monitor->IsStarted());
    monitor->Stop();
    EXPECT_FALSE(monitor->IsStarted());
    EXPECT_TRUE(monitor->IsMetricMode());
}

TEST(MsptiMonitorTest, StartWithJsonlAndDbExportType)
{
    auto *monitor = MsptiMonitor::GetInstance();
    TempDir tmp;

    // Jsonl 导出：落盘模式创建 JsonlProcessManager（空轮转参数走默认值，生产真实路径）
    ASSERT_TRUE(monitor->CheckAndSetSavePath(tmp.Path() + "/jsonl"));
    monitor->SetExportType(MSPTI_EXPORT_TYPE_JSONL);
    monitor->Start();
    EXPECT_TRUE(monitor->IsStarted());
    monitor->Stop();
    EXPECT_FALSE(monitor->IsStarted());

    // DB 导出：落盘模式创建 DBProcessManager 并正常启停
    ASSERT_TRUE(monitor->CheckAndSetSavePath(tmp.Path() + "/db"));
    monitor->SetExportType(MSPTI_EXPORT_TYPE_DB);
    monitor->Start();
    EXPECT_TRUE(monitor->IsStarted());
    monitor->Stop();
    EXPECT_FALSE(monitor->IsStarted());

    // Uninit 清理 savePath_：恢复 metric 模式
    EXPECT_TRUE(monitor->IsMetricMode());
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
