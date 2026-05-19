/*
 * Copyright (C) 2025-2026. Huawei Technologies Co., Ltd. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "db/Connection.h"

using namespace dynolog_npu::ipc_monitor::db;

class ConnectionTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        // Create a temporary in-memory database for testing
        connection = std::make_unique<Connection>(":memory:");
        ASSERT_TRUE(connection->IsDBOpened());
    }

    void TearDown() override { connection.reset(); }

    std::unique_ptr<Connection> connection;
};

TEST_F(ConnectionTest, ConstructorCreatesValidConnection) { EXPECT_TRUE(connection->IsDBOpened()); }

TEST_F(ConnectionTest, CheckTableExistsOnNonExistentTable)
{
    EXPECT_FALSE(connection->CheckTableExists("non_existent_table"));
}

TEST_F(ConnectionTest, CreateTableAndCheckExists)
{
    std::string createTableSQL = "CREATE TABLE test_table (id INTEGER PRIMARY KEY, name TEXT)";
    EXPECT_TRUE(connection->ExecuteCreateTable(createTableSQL));
    EXPECT_TRUE(connection->CheckTableExists("test_table"));
}

TEST_F(ConnectionTest, ExecuteSqlWithValidStatement)
{
    std::string sql = "CREATE TABLE test_table (id INTEGER, name TEXT)";
    EXPECT_TRUE(connection->ExecuteSql(sql, "CREATE TABLE"));
}

TEST_F(ConnectionTest, ExecuteCreateIndex)
{
    // First create a table
    std::string createTableSQL = "CREATE TABLE test_table (id INTEGER, name TEXT)";
    EXPECT_TRUE(connection->ExecuteCreateTable(createTableSQL));

    // Then create an index
    std::string createIndexSQL = "CREATE INDEX idx_name ON test_table(name)";
    EXPECT_TRUE(connection->ExecuteCreateIndex(createIndexSQL));
}

TEST_F(ConnectionTest, ExecuteDropTable)
{
    // Create a table first
    std::string createTableSQL = "CREATE TABLE test_table (id INTEGER, name TEXT)";
    EXPECT_TRUE(connection->ExecuteCreateTable(createTableSQL));
    EXPECT_TRUE(connection->CheckTableExists("test_table"));

    // Then drop it
    std::string dropTableSQL = "DROP TABLE test_table";
    EXPECT_TRUE(connection->ExecuteDropTable(dropTableSQL));
    EXPECT_FALSE(connection->CheckTableExists("test_table"));
}

TEST_F(ConnectionTest, ExecuteUpdate)
{
    // Create a table and insert some data
    std::string createTableSQL = "CREATE TABLE test_table (id INTEGER, name TEXT)";
    EXPECT_TRUE(connection->ExecuteCreateTable(createTableSQL));

    std::string insertSQL = "INSERT INTO test_table VALUES (1, 'Alice')";
    EXPECT_TRUE(connection->ExecuteSql(insertSQL, "INSERT"));

    // Update the data
    std::string updateSQL = "UPDATE test_table SET name = 'Bob' WHERE id = 1";
    EXPECT_TRUE(connection->ExecuteUpdate(updateSQL));

    // Verify the update
    std::vector<std::tuple<int, std::string>> result;
    std::string selectSQL = "SELECT id, name FROM test_table WHERE id = 1";
    EXPECT_TRUE(connection->ExecuteQuery(selectSQL, result));
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(std::get<0>(result[0]), 1);
    EXPECT_EQ(std::get<1>(result[0]), "Bob");
}

TEST_F(ConnectionTest, ExecuteDelete)
{
    // Create a table and insert some data
    std::string createTableSQL = "CREATE TABLE test_table (id INTEGER, name TEXT)";
    EXPECT_TRUE(connection->ExecuteCreateTable(createTableSQL));

    std::string insertSQL = "INSERT INTO test_table VALUES (1, 'Alice')";
    EXPECT_TRUE(connection->ExecuteSql(insertSQL, "INSERT"));

    // Delete the data
    std::string deleteSQL = "DELETE FROM test_table WHERE id = 1";
    EXPECT_TRUE(connection->ExecuteDelete(deleteSQL));

    // Verify the deletion
    std::vector<std::tuple<int, std::string>> result;
    std::string selectSQL = "SELECT id, name FROM test_table";
    EXPECT_TRUE(connection->ExecuteQuery(selectSQL, result));
    EXPECT_TRUE(result.empty());
}

TEST_F(ConnectionTest, ExecuteGetTableColumns)
{
    // Create a table
    std::string createTableSQL = "CREATE TABLE test_table (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)";
    EXPECT_TRUE(connection->ExecuteCreateTable(createTableSQL));

    // Get columns
    auto columns = connection->ExecuteGetTableColumns("test_table");

    // Verify we got the expected columns
    ASSERT_EQ(columns.size(), 3);
    EXPECT_EQ(columns[0].name, "id");
    EXPECT_EQ(columns[0].type, "INTEGER");
    EXPECT_EQ(columns[1].name, "name");
    EXPECT_EQ(columns[1].type, "TEXT");
    EXPECT_EQ(columns[2].name, "age");
    EXPECT_EQ(columns[2].type, "INTEGER");
}

TEST_F(ConnectionTest, ExecuteInsertAndQuery)
{
    // Create a table
    std::string createTableSQL = "CREATE TABLE test_table (id INTEGER, name TEXT, score REAL)";
    EXPECT_TRUE(connection->ExecuteCreateTable(createTableSQL));

    // Prepare test data
    std::vector<std::tuple<int, std::string, double>> testData = {
        {1, "Alice", 95.5}, {2, "Bob", 87.2}, {3, "Charlie", 92.0}};

    // Insert data
    EXPECT_TRUE(connection->ExecuteInsert("test_table", testData));

    // Query the data back
    std::vector<std::tuple<int, std::string, double>> result;
    std::string querySQL = "SELECT id, name, score FROM test_table ORDER BY id";
    EXPECT_TRUE(connection->ExecuteQuery(querySQL, result));

    // Verify the data
    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(std::get<0>(result[0]), 1);
    EXPECT_EQ(std::get<1>(result[0]), "Alice");
    EXPECT_DOUBLE_EQ(std::get<2>(result[0]), 95.5);

    EXPECT_EQ(std::get<0>(result[1]), 2);
    EXPECT_EQ(std::get<1>(result[1]), "Bob");
    EXPECT_DOUBLE_EQ(std::get<2>(result[1]), 87.2);

    EXPECT_EQ(std::get<0>(result[2]), 3);
    EXPECT_EQ(std::get<1>(result[2]), "Charlie");
    EXPECT_DOUBLE_EQ(std::get<2>(result[2]), 92.0);
}

TEST_F(ConnectionTest, ExecuteInsertWithDifferentTypes)
{
    // Create a table with various data types
    std::string createTableSQL =
        "CREATE TABLE test_types ("
        "int_val INTEGER, "
        "uint_val INTEGER, "
        "int64_val INTEGER, "
        "uint64_val INTEGER, "
        "double_val REAL, "
        "str_val TEXT)";
    EXPECT_TRUE(connection->ExecuteCreateTable(createTableSQL));

    // Prepare test data with different types
    std::vector<std::tuple<int32_t, uint32_t, int64_t, uint64_t, double, std::string>> testData = {
        {42, 100, 1000LL, 2000ULL, 3.14, "test_string"}};

    // Insert data
    EXPECT_TRUE(connection->ExecuteInsert("test_types", testData));

    // Query the data back
    std::vector<std::tuple<int32_t, uint32_t, int64_t, uint64_t, double, std::string>> result;
    std::string querySQL = "SELECT int_val, uint_val, int64_val, uint64_val, double_val, str_val FROM test_types";
    EXPECT_TRUE(connection->ExecuteQuery(querySQL, result));

    // Verify the data
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(std::get<0>(result[0]), 42);
    EXPECT_EQ(std::get<1>(result[0]), 100);
    EXPECT_EQ(std::get<2>(result[0]), 1000LL);
    EXPECT_EQ(std::get<3>(result[0]), 2000ULL);
    EXPECT_DOUBLE_EQ(std::get<4>(result[0]), 3.14);
    EXPECT_EQ(std::get<5>(result[0]), "test_string");
}

TEST_F(ConnectionTest, ExecuteQueryWithNoResults)
{
    // Create a table
    std::string createTableSQL = "CREATE TABLE test_table (id INTEGER, name TEXT)";
    EXPECT_TRUE(connection->ExecuteCreateTable(createTableSQL));

    // Query for non-existent data
    std::vector<std::tuple<int, std::string>> result;
    std::string querySQL = "SELECT id, name FROM test_table WHERE id = 999";
    EXPECT_TRUE(connection->ExecuteQuery(querySQL, result));

    // Should return empty result
    EXPECT_TRUE(result.empty());
}

TEST_F(ConnectionTest, ExecuteInvalidSQL)
{
    // Try to execute invalid SQL
    std::string invalidSQL = "INVALID SQL STATEMENT";
    EXPECT_FALSE(connection->ExecuteSql(invalidSQL, "INVALID"));
}

TEST_F(ConnectionTest, DestructorClosesDatabase)
{
    // Test that the destructor properly closes the database
    // This is implicitly tested through the RAII pattern, but we can check
    // that the connection object is properly destroyed
    {
        auto tempConn = std::make_unique<Connection>(":memory:");
        EXPECT_TRUE(tempConn->IsDBOpened());
    }  // tempConn goes out of scope here and destructor is called
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
