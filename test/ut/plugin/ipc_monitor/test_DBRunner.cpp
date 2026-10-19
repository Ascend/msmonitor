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
#include <string>
#include <tuple>
#include <vector>

#include "plugin/ipc_monitor/db/DBRunner.h"

using dynolog_npu::ipc_monitor::db::DBRunner;
using dynolog_npu::ipc_monitor::db::TableColumn;

namespace
{

class TempDir
{
   public:
    TempDir()
    {
        std::string tmpl = "/tmp/msmonitor_dbr_XXXXXX";
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

class DBRunnerTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        ASSERT_FALSE(dir_.Path().empty());
        dbPath_ = dir_.Path() + "/ut.db";
    }

    TempDir dir_;
    std::string dbPath_;
};

TEST_F(DBRunnerTest, BadPathReturnsFalse)
{
    DBRunner runner("/no/such/msmonitor_ut_dir/missing.db");
    EXPECT_FALSE(runner.CheckTableExists("t"));
    EXPECT_FALSE(runner.CreateTable("t", {TableColumn("id", "INTEGER")}));
    EXPECT_FALSE(runner.CreateIndex("t", "idx", {"id"}));
    EXPECT_FALSE(runner.DropTable("t"));
    EXPECT_FALSE(runner.DeleteData("DELETE FROM t"));
    EXPECT_FALSE(runner.UpdateData("UPDATE t SET id = 1"));
    EXPECT_TRUE(runner.GetTableColumns("t").empty());
    std::vector<std::tuple<int>> rows;
    EXPECT_FALSE(runner.QueryData("SELECT 1", rows));
    EXPECT_FALSE(runner.InsertData("t", std::vector<std::tuple<int>>{{1}}));
}

TEST_F(DBRunnerTest, EmptyNamesRejected)
{
    DBRunner runner(dbPath_);
    EXPECT_FALSE(runner.CreateTable("", {TableColumn("id", "INTEGER")}));
    EXPECT_FALSE(runner.DropTable(""));
    EXPECT_FALSE(runner.CreateIndex("", "idx", {"id"}));
    EXPECT_FALSE(runner.CreateIndex("t", "", {"id"}));
    EXPECT_FALSE(runner.CreateIndex("t", "idx", {}));
    EXPECT_FALSE(runner.InsertData("", std::vector<std::tuple<int>>{{1}}));
}

TEST_F(DBRunnerTest, CreateInsertQueryUpdateDelete)
{
    DBRunner runner(dbPath_);
    EXPECT_FALSE(runner.CheckTableExists("sample"));
    ASSERT_TRUE(runner.CreateTable(
        "sample", {TableColumn("id", "INTEGER", true), TableColumn("name", "TEXT"), TableColumn("score", "REAL")}));
    EXPECT_TRUE(runner.CheckTableExists("sample"));
    ASSERT_TRUE(runner.CreateIndex("sample", "idx_name", {"name"}));

    auto cols = runner.GetTableColumns("sample");
    ASSERT_EQ(cols.size(), 3u);
    EXPECT_EQ(cols[0].name, "id");
    EXPECT_TRUE(runner.GetTableColumns("missing").empty());

    std::vector<std::tuple<int, std::string, double>> data = {{1, "Alice", 1.5}, {2, "Bob", 2.5}};
    ASSERT_TRUE(runner.InsertData("sample", data));

    std::vector<std::tuple<int, std::string, double>> got;
    ASSERT_TRUE(runner.QueryData("SELECT id, name, score FROM sample ORDER BY id", got));
    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(std::get<1>(got[0]), "Alice");

    ASSERT_TRUE(runner.UpdateData("UPDATE sample SET name = 'Ann' WHERE id = 1"));
    got.clear();
    ASSERT_TRUE(runner.QueryData("SELECT id, name, score FROM sample WHERE id = 1", got));
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(std::get<1>(got[0]), "Ann");

    ASSERT_TRUE(runner.DeleteData("DELETE FROM sample WHERE id = 2"));
    got.clear();
    ASSERT_TRUE(runner.QueryData("SELECT id, name, score FROM sample", got));
    EXPECT_EQ(got.size(), 1u);

    EXPECT_FALSE(runner.QueryData("SELECT no_such FROM sample", got));
    EXPECT_FALSE(runner.UpdateData("UPDATE missing SET id = 1"));
    EXPECT_FALSE(runner.DeleteData("DELETE FROM missing"));
    EXPECT_FALSE(runner.InsertData("missing", data));

    ASSERT_TRUE(runner.DropTable("sample"));
    EXPECT_FALSE(runner.CheckTableExists("sample"));
    EXPECT_FALSE(runner.DropTable("sample"));
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
