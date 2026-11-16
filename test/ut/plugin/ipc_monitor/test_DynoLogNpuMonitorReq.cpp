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

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mockcpp/mockcpp.hpp>
#include <string>
#include <vector>

#include "plugin/ipc_monitor/DynoLogNpuMonitor.h"
#include "plugin/ipc_monitor/mspti_monitor/MsptiMonitor.h"
#include "plugin/ipc_monitor/utils/utils.h"

using namespace dynolog_npu::ipc_monitor;

namespace
{

class TempDir
{
   public:
    TempDir()
    {
        std::string tmpl = "/tmp/msmonitor_dynomon_XXXXXX";
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

}  // namespace

// DynoLogNpuMonitor::DealMonitorReq 与 EnableMsptiMonitor 此前零覆盖：
// 此处验证停止幂等、启动/活动差异同步（set_difference 双向）、非法保存路径拒绝与配置字典全链路

class DynoLogNpuMonitorReqTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        // DealMonitorReq 启动路径末尾会调用 GetRankId（内部 acquire GIL 并 import Python 模块），
        // UT 二进制无 Python 解释器，必须 stub 为固定值避免段错误
        MOCKER_CPP(&GetRankId).stubs().will(returnValue(0));
    }

    void TearDown() override { GlobalMockObject::verify(); }
};

TEST_F(DynoLogNpuMonitorReqTest, DealMonitorReqStopWithoutStart)
{
    auto *monitor = DynoLogNpuMonitor::GetInstance();
    MsptiMonitorCfg cmd{};
    cmd.monitorStop = true;
    EXPECT_EQ(monitor->DealMonitorReq(cmd), ErrCode::SUC);
    EXPECT_FALSE(MsptiMonitor::GetInstance()->IsStarted());
}

TEST_F(DynoLogNpuMonitorReqTest, DealMonitorReqStartNarrowAndStop)
{
    auto *monitor = DynoLogNpuMonitor::GetInstance();
    auto *mspti = MsptiMonitor::GetInstance();
    EXPECT_FALSE(mspti->IsStarted());

    // metric 模式启动：使能 KERNEL+API 并下发过滤项
    MsptiMonitorCfg start{};
    start.monitorStart = true;
    start.enableActivities = {MSPTI_ACTIVITY_KIND_KERNEL, MSPTI_ACTIVITY_KIND_API};
    start.filterItems[MSPTI_ACTIVITY_KIND_KERNEL] = {"add"};
    EXPECT_EQ(monitor->DealMonitorReq(start), ErrCode::SUC);
    EXPECT_TRUE(mspti->IsStarted());
    EXPECT_EQ(mspti->GetEnabledActivities().size(), 2u);

    // 收敛为仅 KERNEL：API 由 set_difference 下发 DisableActivity
    MsptiMonitorCfg narrow{};
    narrow.monitorStart = false;
    narrow.enableActivities = {MSPTI_ACTIVITY_KIND_KERNEL};
    EXPECT_EQ(monitor->DealMonitorReq(narrow), ErrCode::SUC);
    EXPECT_EQ(mspti->GetEnabledActivities().size(), 1u);
    EXPECT_EQ(mspti->GetEnabledActivities().count(MSPTI_ACTIVITY_KIND_KERNEL), 1u);

    MsptiMonitorCfg stop{};
    stop.monitorStop = true;
    EXPECT_EQ(monitor->DealMonitorReq(stop), ErrCode::SUC);
    EXPECT_FALSE(mspti->IsStarted());
}

TEST_F(DynoLogNpuMonitorReqTest, DealMonitorReqInvalidSavePathRejected)
{
    auto *monitor = DynoLogNpuMonitor::GetInstance();
    TempDir tmp;
    const std::string file = tmp.Path() + "/plain";
    {
        std::ofstream ofs(file, std::ios::out);
        ofs << "x";
    }

    // 普通文件不是合法保存目录：PERMISSION，且不拉起采集
    MsptiMonitorCfg cmd{};
    cmd.monitorStart = true;
    cmd.savePath = file;
    EXPECT_EQ(monitor->DealMonitorReq(cmd), ErrCode::PERMISSION);
    EXPECT_FALSE(MsptiMonitor::GetInstance()->IsStarted());
}

TEST_F(DynoLogNpuMonitorReqTest, EnableMsptiMonitorConfigMapLifecycle)
{
    auto *monitor = DynoLogNpuMonitor::GetInstance();
    auto *mspti = MsptiMonitor::GetInstance();
    std::unordered_map<std::string, std::string> cfg = {
        {"MSPTI_ACTIVITY_KIND", "Kernel"}, {"REPORT_INTERVAL_S", "1"},          {"DURATION", "0"},
        {"NPU_MONITOR_START", "true"},     {"NPU_MONITOR_STOP", "false"},       {"NPU_MONITOR_LOG_FILE", ""},
        {"NPU_MONITOR_EXPORT_TYPE", "DB"}, {"NPU_MONITOR_FILTER", "Kernel:add"}};

    // savePath 为空 → metric 模式启动；经 InputParser 校验与状态上报全链路
    monitor->EnableMsptiMonitor(cfg);
    EXPECT_TRUE(mspti->IsStarted());
    EXPECT_EQ(mspti->GetEnabledActivities().size(), 1u);

    cfg["NPU_MONITOR_START"] = "false";
    cfg["NPU_MONITOR_STOP"] = "true";
    monitor->EnableMsptiMonitor(cfg);
    EXPECT_FALSE(mspti->IsStarted());
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
