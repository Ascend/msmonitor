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

#include "db/DBRunner.h"

#include <algorithm>

namespace dynolog_npu
{
namespace ipc_monitor
{
namespace db
{
namespace
{
std::string GetColumnsString(const std::vector<TableColumn> &columns)
{
    std::vector<std::string> columnStrings(columns.size());
    std::transform(columns.begin(), columns.end(), columnStrings.begin(),
                   [](const TableColumn &column) { return column.ToString(); });
    return join(columnStrings, ",");
}
}  // namespace

bool DBRunner::CheckTableExists(const std::string &tableName) const
{
    std::shared_ptr<Connection> conn{nullptr};
    MakeSharedPtr(conn, path_);
    if (conn == nullptr || !conn->IsDBOpened())
    {
        LOG(ERROR) << "Create connection failed: " << path_;
        return false;
    }
    return conn->CheckTableExists(tableName);
}

bool DBRunner::CreateTable(const std::string &tableName, const std::vector<TableColumn> &columns) const
{
    if (tableName.empty())
    {
        LOG(ERROR) << "Create table failed, table name is empty";
        return false;
    }
    std::shared_ptr<Connection> conn{nullptr};
    MakeSharedPtr(conn, path_);
    if (conn == nullptr || !conn->IsDBOpened())
    {
        LOG(ERROR) << "Create connection failed: " << path_;
        return false;
    }
    LOG(INFO) << "Create table " << tableName;
    std::string columnsString = GetColumnsString(columns);
    std::string sql = "CREATE TABLE IF NOT EXISTS " + tableName + " (" + columnsString + ")";
    if (!conn->ExecuteCreateTable(sql))
    {
        LOG(ERROR) << "Create table " << tableName << " failed";
        return false;
    }
    LOG(INFO) << "Create table " << tableName << " success";
    return true;
}

bool DBRunner::CreateIndex(const std::string &tableName, const std::string &indexName,
                           const std::vector<std::string> &colNames) const
{
    if (tableName.empty() || indexName.empty() || colNames.empty())
    {
        LOG(ERROR) << "Create index failed, table name or index name or column name is empty";
        return false;
    }
    std::shared_ptr<Connection> conn{nullptr};
    MakeSharedPtr(conn, path_);
    if (conn == nullptr || !conn->IsDBOpened())
    {
        LOG(ERROR) << "Create connection failed: " << path_;
        return false;
    }
    LOG(INFO) << "Create index " << indexName << " on table " << tableName;
    std::string valueStr = join(colNames, ",");
    std::string sql = "CREATE INDEX IF NOT EXISTS " + indexName + " ON " + tableName + " (" + valueStr + ")";
    if (!conn->ExecuteCreateIndex(sql))
    {
        LOG(ERROR) << "Create index " << indexName << " on table " << tableName << " failed, sql: " << sql;
        return false;
    }
    LOG(INFO) << "Create index " << indexName << " on table " << tableName << " success";
    return true;
}

bool DBRunner::DropTable(const std::string &tableName) const
{
    if (tableName.empty())
    {
        LOG(ERROR) << "Drop table failed, table name is empty";
        return false;
    }
    std::shared_ptr<Connection> conn{nullptr};
    MakeSharedPtr(conn, path_);
    if (conn == nullptr || !conn->IsDBOpened())
    {
        LOG(ERROR) << "Create connection failed: " << path_;
        return false;
    }
    LOG(INFO) << "Drop table " << tableName;
    std::string sql = "DROP TABLE " + tableName;
    if (!conn->ExecuteDropTable(sql))
    {
        LOG(ERROR) << "Drop table " << tableName << " failed";
        return false;
    }
    LOG(INFO) << "Drop table " << tableName << " success";
    return true;
}

bool DBRunner::DeleteData(const std::string &sql) const
{
    std::shared_ptr<Connection> conn{nullptr};
    MakeSharedPtr(conn, path_);
    if (conn == nullptr || !conn->IsDBOpened())
    {
        LOG(ERROR) << "Create connection failed: " << path_;
        return false;
    }
    LOG(INFO) << "Delete data, sql: " << sql;
    if (!conn->ExecuteDelete(sql))
    {
        LOG(ERROR) << "Delete data failed, sql: " << sql;
        return false;
    }
    LOG(INFO) << "Delete data success, sql: " << sql;
    return true;
}

bool DBRunner::UpdateData(const std::string &sql) const
{
    std::shared_ptr<Connection> conn{nullptr};
    MakeSharedPtr(conn, path_);
    if (conn == nullptr || !conn->IsDBOpened())
    {
        LOG(ERROR) << "Create connection failed: " << path_;
        return false;
    }
    LOG(INFO) << "Update data, sql: " << sql;
    if (!conn->ExecuteUpdate(sql))
    {
        LOG(ERROR) << "Update data failed, sql: " << sql;
        return false;
    }
    LOG(INFO) << "Update data success, sql: " << sql;
    return true;
}

std::vector<TableColumn> DBRunner::GetTableColumns(const std::string &tableName) const
{
    std::shared_ptr<Connection> conn{nullptr};
    MakeSharedPtr(conn, path_);
    if (conn == nullptr || !conn->IsDBOpened())
    {
        LOG(ERROR) << "Create connection failed: " << path_;
        return {};
    }
    LOG(INFO) << "Get table columns, table name: " << tableName;
    auto cols = conn->ExecuteGetTableColumns(tableName);
    if (cols.empty())
    {
        LOG(ERROR) << "Get table columns failed, table name: " << tableName;
        return cols;
    }
    LOG(INFO) << "Get table columns success, table name: " << tableName;
    return cols;
}
}  // namespace db
}  // namespace ipc_monitor
}  // namespace dynolog_npu
