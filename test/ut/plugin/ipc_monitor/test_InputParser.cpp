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
#include <linux/limits.h>

#include <string>
#include <unordered_map>

#include "plugin/ipc_monitor/InputParser.h"

using dynolog_npu::ipc_monitor::InputParser;

namespace
{

const char *kAllKinds = "Marker,Kernel,API,Hccl,Memory,MemSet,MemCpy,Communication,AclAPI,NodeAPI,RuntimeAPI";

std::unordered_map<std::string, std::string> ValidCmd()
{
    return {{"MSPTI_ACTIVITY_KIND", kAllKinds},
            {"REPORT_INTERVAL_S", "15"},
            {"DURATION", "1.5"},
            {"NPU_MONITOR_START", "TRUE"},
            {"NPU_MONITOR_STOP", "False"},
            {"NPU_MONITOR_LOG_FILE", "/tmp/msmonitor_ut"},
            {"NPU_MONITOR_EXPORT_TYPE", "DB"},
            {"NPU_MONITOR_FILTER", "Kernel:matmul,add;Unknown:x;bad;API:launch"},
            {"JSONL_ROTATE_LOG_LINES", "200"},
            {"JSONL_ROTATE_LOG_FILES", "3"}};
}

}  // namespace

class InputParserTest : public ::testing::Test
{
};

// 1. 合法参数：全部活动类型、过滤项和轮转参数
TEST_F(InputParserTest, ValidArgsParsed)
{
    auto cmd = ValidCmd();
    auto cfg = InputParser::GetInstance()->DynoLogGetOpts(cmd);

    EXPECT_TRUE(cfg.isMonitor);
    EXPECT_EQ(cfg.enableActivities.size(), 11u);
    EXPECT_EQ(cfg.enableActivities.count(MSPTI_ACTIVITY_KIND_MARKER), 1u);
    EXPECT_EQ(cfg.enableActivities.count(MSPTI_ACTIVITY_KIND_KERNEL), 1u);
    EXPECT_EQ(cfg.reportIntervals, 15u);
    EXPECT_FLOAT_EQ(cfg.duration, 1.5f);
    EXPECT_TRUE(cfg.monitorStart);
    EXPECT_FALSE(cfg.monitorStop);
    EXPECT_EQ(cfg.savePath, "/tmp/msmonitor_ut");
    EXPECT_EQ(cfg.export_type, "DB");
    EXPECT_EQ(cfg.json_rotate_log_lines, "200");
    EXPECT_EQ(cfg.json_rotate_log_files, "3");
    ASSERT_EQ(cfg.filterItems.count(MSPTI_ACTIVITY_KIND_KERNEL), 1u);
    EXPECT_EQ(cfg.filterItems[MSPTI_ACTIVITY_KIND_KERNEL].count("matmul"), 1u);
    EXPECT_EQ(cfg.filterItems[MSPTI_ACTIVITY_KIND_KERNEL].count("add"), 1u);
    ASSERT_EQ(cfg.filterItems.count(MSPTI_ACTIVITY_KIND_API), 1u);
    EXPECT_EQ(cfg.filterItems[MSPTI_ACTIVITY_KIND_API].count("launch"), 1u);
    EXPECT_EQ(cfg.filterItems.count(MSPTI_ACTIVITY_KIND_INVALID), 0u);
}

TEST_F(InputParserTest, JsonlExportAndLowerCaseBool)
{
    auto cmd = ValidCmd();
    cmd["NPU_MONITOR_EXPORT_TYPE"] = "Jsonl";
    cmd["NPU_MONITOR_START"] = "false";
    cmd["NPU_MONITOR_STOP"] = "true";
    cmd["DURATION"] = "-2.5";
    cmd["MSPTI_ACTIVITY_KIND"] = "Kernel";
    cmd.erase("NPU_MONITOR_FILTER");

    auto cfg = InputParser::GetInstance()->DynoLogGetOpts(cmd);
    EXPECT_TRUE(cfg.isMonitor);
    EXPECT_FALSE(cfg.monitorStart);
    EXPECT_TRUE(cfg.monitorStop);
    EXPECT_FLOAT_EQ(cfg.duration, -2.5f);
    EXPECT_EQ(cfg.export_type, "Jsonl");
    EXPECT_EQ(cfg.enableActivities.size(), 1u);
    EXPECT_TRUE(cfg.filterItems.empty());
    EXPECT_EQ(cfg.json_rotate_log_lines, "200");
}

// 2. 缺必填项或取值非法时返回无效配置
TEST_F(InputParserTest, MissingRequiredKeyInvalid)
{
    auto cmd = ValidCmd();
    cmd.erase("DURATION");
    auto cfg = InputParser::GetInstance()->DynoLogGetOpts(cmd);
    EXPECT_FALSE(cfg.isMonitor);
    EXPECT_EQ(cfg.enableActivities.count(MSPTI_ACTIVITY_KIND_INVALID), 1u);
    EXPECT_EQ(cfg.reportIntervals, 0u);
    EXPECT_TRUE(cfg.savePath.empty());
}

TEST_F(InputParserTest, InvalidKindUintFloatBoolExportAndPath)
{
    auto parser = InputParser::GetInstance();

    auto badKind = ValidCmd();
    badKind["MSPTI_ACTIVITY_KIND"] = "Kernel,Foo";
    EXPECT_FALSE(parser->DynoLogGetOpts(badKind).isMonitor);

    auto badUint = ValidCmd();
    badUint["REPORT_INTERVAL_S"] = "4294967296";
    EXPECT_FALSE(parser->DynoLogGetOpts(badUint).isMonitor);

    auto overflowUint = ValidCmd();
    overflowUint["REPORT_INTERVAL_S"] = "18446744073709551616";
    EXPECT_FALSE(parser->DynoLogGetOpts(overflowUint).isMonitor);

    auto badFloat = ValidCmd();
    badFloat["DURATION"] = ".";
    EXPECT_FALSE(parser->DynoLogGetOpts(badFloat).isMonitor);

    auto badBool = ValidCmd();
    badBool["NPU_MONITOR_START"] = "yes";
    EXPECT_FALSE(parser->DynoLogGetOpts(badBool).isMonitor);

    auto badExport = ValidCmd();
    badExport["NPU_MONITOR_EXPORT_TYPE"] = "csv";
    EXPECT_FALSE(parser->DynoLogGetOpts(badExport).isMonitor);

    auto badPath = ValidCmd();
    badPath["NPU_MONITOR_LOG_FILE"] = std::string(PATH_MAX + 1, 'a');
    EXPECT_FALSE(parser->DynoLogGetOpts(badPath).isMonitor);
}

TEST_F(InputParserTest, EmptyKindListStillValid)
{
    auto cmd = ValidCmd();
    cmd["MSPTI_ACTIVITY_KIND"] = "";
    auto cfg = InputParser::GetInstance()->DynoLogGetOpts(cmd);
    EXPECT_TRUE(cfg.isMonitor);
    EXPECT_TRUE(cfg.enableActivities.empty());
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
