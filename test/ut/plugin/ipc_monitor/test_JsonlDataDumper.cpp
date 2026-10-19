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
#include <memory>
#include <mockcpp/mockcpp.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "plugin/ipc_monitor/jsonl/JsonlDataDumper.h"
#include "plugin/ipc_monitor/utils/utils.h"

using dynolog_npu::ipc_monitor::GetRankId;
using dynolog_npu::ipc_monitor::jsonl::JsonlDataDumper;

namespace
{

class TempDir
{
   public:
    TempDir()
    {
        std::string tmpl = "/tmp/msmonitor_jsonl_XXXXXX";
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

class JsonlDataDumperTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        ASSERT_FALSE(dir_.Path().empty());
        MOCKER_CPP(&GetRankId).stubs().will(returnValue(-1));
    }

    void TearDown() override { GlobalMockObject::verify(); }

    TempDir dir_;
};

TEST_F(JsonlDataDumperTest, RecordBeforeStartAndNullAreIgnored)
{
    JsonlDataDumper dumper;
    dumper.Record(nullptr);
    dumper.Record(std::make_unique<nlohmann::json>(nlohmann::json{{"k", 1}}));
    dumper.Stop();
    dumper.UnInit();
}

TEST_F(JsonlDataDumperTest, InitStartRecordStopWritesFile)
{
    JsonlDataDumper dumper;
    dumper.Init(dir_.Path(), 16, 1000, "200", "3");
    dumper.Start();
    dumper.Record(nullptr);
    auto row = std::make_unique<nlohmann::json>();
    (*row)["kind"] = "Kernel";
    (*row)["name"] = "matmul";
    dumper.Record(std::move(row));
    dumper.Stop();

    bool found = false;
    for (const auto &ent : std::filesystem::directory_iterator(dir_.Path()))
    {
        if (ent.path().extension() == ".jsonl")
        {
            found = true;
        }
    }
    EXPECT_TRUE(found);
    dumper.UnInit();
}

TEST_F(JsonlDataDumperTest, RotateParamsClampAndInvalidFallback)
{
    JsonlDataDumper low;
    low.Init(dir_.Path(), 16, 1000, "50", "1");
    low.Start();
    low.Stop();

    JsonlDataDumper high;
    high.Init(dir_.Path(), 16, 1000, "600000", "-1");
    high.Start();
    high.Stop();

    JsonlDataDumper bad;
    bad.Init(dir_.Path(), 16, 1000, "abc", "zz");
    bad.Start();
    bad.Record(std::make_unique<nlohmann::json>(nlohmann::json{{"a", 1}}));
    bad.Stop();
}

TEST_F(JsonlDataDumperTest, EmptyParamReadsEnvironment)
{
    ASSERT_EQ(setenv("MSMONITOR_JSONL_ROTATE_LOG_LINES", "4096", 1), 0);
    ASSERT_EQ(setenv("MSMONITOR_JSONL_ROTATE_LOG_FILES", "4", 1), 0);
    JsonlDataDumper fromEnv;
    fromEnv.Init(dir_.Path(), 16, 1000, "", "");
    fromEnv.Start();
    fromEnv.Stop();

    ASSERT_EQ(setenv("MSMONITOR_JSONL_ROTATE_LOG_LINES", "nope", 1), 0);
    ASSERT_EQ(setenv("MSMONITOR_JSONL_ROTATE_LOG_FILES", "0", 1), 0);
    JsonlDataDumper badEnv;
    badEnv.Init(dir_.Path(), 16, 1000, "", "");
    badEnv.Start();
    badEnv.Stop();

    unsetenv("MSMONITOR_JSONL_ROTATE_LOG_LINES");
    unsetenv("MSMONITOR_JSONL_ROTATE_LOG_FILES");
    JsonlDataDumper defaults;
    defaults.Init(dir_.Path(), 16, 1000, "", "");
    defaults.Start();
    defaults.Stop();
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
