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

#include "plugin/ipc_monitor/db/DBConstant.h"
#include "plugin/ipc_monitor/db/DataBase.h"

using dynolog_npu::ipc_monitor::db::Database;
using dynolog_npu::ipc_monitor::db::MsMonitorDB;
using dynolog_npu::ipc_monitor::db::TABLE_API_TYPE;
using dynolog_npu::ipc_monitor::db::TABLE_CANN_API;
using dynolog_npu::ipc_monitor::db::TABLE_COMMUNICATION_OP;
using dynolog_npu::ipc_monitor::db::TABLE_COMPUTE_TASK_INFO;
using dynolog_npu::ipc_monitor::db::TABLE_HOST_INFO;
using dynolog_npu::ipc_monitor::db::TABLE_META_DATA;
using dynolog_npu::ipc_monitor::db::TABLE_MSTX;
using dynolog_npu::ipc_monitor::db::TABLE_NPU_INFO;
using dynolog_npu::ipc_monitor::db::TABLE_RANK_DEVICE_MAP;
using dynolog_npu::ipc_monitor::db::TABLE_SESSION_TIME_INFO;
using dynolog_npu::ipc_monitor::db::TABLE_STRING_IDS;
using dynolog_npu::ipc_monitor::db::TABLE_TASK;

TEST(DataBaseTest, DefaultNameAndMissingTable)
{
    Database db;
    EXPECT_TRUE(db.GetDBName().empty());
    db.SetDBName("custom.db");
    EXPECT_EQ(db.GetDBName(), "custom.db");
    // Missing table returns a reference to a temporary. Do not use the result.
    (void)db.GetTableCols("NO_SUCH_TABLE");
}

TEST(DataBaseTest, MsMonitorSchemaColumns)
{
    MsMonitorDB db;
    EXPECT_EQ(db.GetDBName(), "msmonitor.db");

    EXPECT_EQ(db.GetTableCols(TABLE_STRING_IDS).size(), 2u);
    EXPECT_EQ(db.GetTableCols(TABLE_SESSION_TIME_INFO).size(), 2u);
    EXPECT_EQ(db.GetTableCols(TABLE_CANN_API).size(), 6u);
    EXPECT_EQ(db.GetTableCols(TABLE_TASK).size(), 11u);
    EXPECT_EQ(db.GetTableCols(TABLE_COMPUTE_TASK_INFO).size(), 15u);
    EXPECT_EQ(db.GetTableCols(TABLE_COMMUNICATION_OP).size(), 13u);
    EXPECT_EQ(db.GetTableCols(TABLE_MSTX).size(), 10u);
    EXPECT_EQ(db.GetTableCols(TABLE_META_DATA).size(), 2u);
    EXPECT_EQ(db.GetTableCols(TABLE_HOST_INFO).size(), 2u);
    EXPECT_EQ(db.GetTableCols(TABLE_NPU_INFO).size(), 2u);
    EXPECT_EQ(db.GetTableCols(TABLE_RANK_DEVICE_MAP).size(), 2u);
    const auto &enumCols = db.GetTableCols(TABLE_API_TYPE);
    ASSERT_EQ(enumCols.size(), 2u);
    EXPECT_TRUE(enumCols[0].isPrimaryKey);
    EXPECT_EQ(enumCols[0].ToString(), "id INTEGER PRIMARY KEY");
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
