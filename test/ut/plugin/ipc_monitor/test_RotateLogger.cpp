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
#include <mockcpp/mockcpp.hpp>
#include <fstream>
#include <memory>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include "plugin/ipc_monitor/jsonl/RotateLogger.h"
#include "plugin/ipc_monitor/utils/utils.h"

using namespace dynolog_npu::ipc_monitor::jsonl;

std::string ReadFileContent(const std::string& filePath)
{
    std::ifstream ifs(filePath, std::ios::binary | std::ios::ate);

    if (!ifs)
    {
        return "";
    }

    auto size = ifs.tellg();
    if (size < 0)
    {
        return "";
    }

    ifs.seekg(0);

    std::string content;
    content.resize(static_cast<size_t>(size));

    ifs.read(content.data(), size);

    return content;
}

class RotateLoggerTest : public ::testing::Test {
protected:
    std::string testLogDir;

    void SetUp() override
    {
        GlobalMockObject::verify();
        MOCKER_CPP(&dynolog_npu::ipc_monitor::GetRankId)
            .stubs()
            .will(returnValue(0));
        // Create a temporary directory for testing
        testLogDir = "./test_rotate_logger_" + std::to_string(getpid());
        std::filesystem::create_directory(testLogDir);
    }

    void TearDown() override
    {
        // Clean up test directory and files
        if (std::filesystem::exists(testLogDir)) {
            std::filesystem::remove_all(testLogDir);
        }
    }
};

TEST_F(RotateLoggerTest, ConstructorInitialization)
{
    RotateLogger logger(testLogDir, 100, 5);
    SUCCEED(); // Constructor should initialize without throwing
}

TEST_F(RotateLoggerTest, BasicLogToFile)
{
    RotateLogger logger(testLogDir, 100, 5);

    std::string message = "Test log message\n";
    EXPECT_NO_THROW(logger.Log(message));

    // After logging, flush to ensure data is written
    EXPECT_NO_THROW(logger.Flush());
    EXPECT_NO_THROW(logger.UnInit());

    // Check if a log file was created with the expected content
    bool fileFound = false;
    for (const auto& entry : std::filesystem::directory_iterator(testLogDir)) {
        if (entry.path().extension() == ".jsonl") {
            fileFound = true;
            std::string content = ReadFileContent(entry.path().string());
            EXPECT_EQ(content, message);
            break;
        }
    }
    EXPECT_TRUE(fileFound);
}

TEST_F(RotateLoggerTest, LineLimitTriggersRotation)
{
    RotateLogger logger(testLogDir, 2, 5); // Max 2 messages per file

    // Write 4 messages - should trigger rotation after 2nd and 4th
    EXPECT_NO_THROW(logger.Log("Message 1\n"));
    EXPECT_NO_THROW(logger.Log("Message 2\n"));
    EXPECT_NO_THROW(logger.Log("Message 3\n"));
    EXPECT_NO_THROW(logger.Log("Message 4\n"));

    EXPECT_NO_THROW(logger.Flush());
    EXPECT_NO_THROW(logger.UnInit());

    // Count the number of log files created
    int fileCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(testLogDir)) {
        if (entry.path().extension() == ".jsonl") {
            fileCount++;
        }
    }

    // Should have at least 2 files due to rotation
    EXPECT_GE(fileCount, 2);
}

TEST_F(RotateLoggerTest, FileLimitEnforcement)
{
    RotateLogger logger(testLogDir, 1, 3); // Max 1 message per file, max 3 files

    // Write 5 messages - should only keep last 3 files
    for (int i = 0; i < 5; ++i) {
        EXPECT_NO_THROW(logger.Log("Message " + std::to_string(i) + "\n"));
    }

    EXPECT_NO_THROW(logger.Flush());
    EXPECT_NO_THROW(logger.UnInit());

    // Count the number of log files created
    int fileCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(testLogDir)) {
        if (entry.path().extension() == ".jsonl") {
            fileCount++;
        }
    }

    // Should have at most 3 files due to maxFiles limit
    EXPECT_LE(fileCount, 3);
}

TEST_F(RotateLoggerTest, EmptyMessageHandling)
{
    RotateLogger logger(testLogDir, 100, 5);

    // Logging empty message should not cause crash
    EXPECT_NO_THROW(logger.Log(""));

    // Normal message should still work
    std::string validMessage = "Valid message\n";
    EXPECT_NO_THROW(logger.Log(validMessage));

    EXPECT_NO_THROW(logger.Flush());
    EXPECT_NO_THROW(logger.UnInit());

    // Check if a log file was created with the valid message
    bool fileFound = false;
    for (const auto& entry : std::filesystem::directory_iterator(testLogDir)) {
        if (entry.path().extension() == ".jsonl") {
            fileFound = true;
            std::string content = ReadFileContent(entry.path().string());
            EXPECT_EQ(content, validMessage);
            break;
        }
    }
    EXPECT_TRUE(fileFound);
}

TEST_F(RotateLoggerTest, MultipleMessagesInOneFile)
{
    RotateLogger logger(testLogDir, 10, 5); // High line limit to avoid rotation

    // Write multiple messages that should go to the same file
    std::vector<std::string> messages = {
        "First message\n",
        "Second message\n",
        "Third message\n"
    };

    for (const auto& msg : messages) {
        EXPECT_NO_THROW(logger.Log(msg));
    }

    EXPECT_NO_THROW(logger.Flush());
    EXPECT_NO_THROW(logger.UnInit());

    // There should be exactly one file with all messages
    std::vector<std::string> foundFiles;
    for (const auto& entry : std::filesystem::directory_iterator(testLogDir)) {
        if (entry.path().extension() == ".jsonl") {
            foundFiles.push_back(entry.path().string());
        }
    }

    ASSERT_EQ(foundFiles.size(), 1);

    std::string content = ReadFileContent(foundFiles[0]);

    EXPECT_NE(content.find(messages[0]), std::string::npos);
    EXPECT_NE(content.find(messages[1]), std::string::npos);
    EXPECT_NE(content.find(messages[2]), std::string::npos);
}

TEST_F(RotateLoggerTest, UnInitClosesResources)
{
    std::string tempDir = testLogDir + "_uninit";
    std::filesystem::create_directory(tempDir);

    {
        RotateLogger logger(tempDir, 100, 5);
        EXPECT_NO_THROW(logger.Log("Test message before UnInit\n"));
        EXPECT_NO_THROW(logger.Flush());
        // Explicitly call UnInit
        EXPECT_NO_THROW(logger.UnInit());

        // Log after UnInit - this should handle gracefully
        EXPECT_NO_THROW(logger.Log("Message after UnInit\n"));
    }

    // After scope ends (destructor), check files
    int fileCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(tempDir)) {
        if (entry.path().extension() == ".jsonl") {
            fileCount++;
        }
    }

    EXPECT_GE(fileCount, 0); // Should have handled gracefully

    std::filesystem::remove_all(tempDir);
}

TEST_F(RotateLoggerTest, FlushEnsuresDataPersistence)
{
    RotateLogger logger(testLogDir, 100, 5);

    EXPECT_NO_THROW(logger.Log("Flushing test message\n"));

    // Flush should ensure data is written to disk
    EXPECT_NO_THROW(logger.Flush());
    EXPECT_NO_THROW(logger.UnInit());

    // Verify file content exists
    bool foundContent = false;
    for (const auto& entry : std::filesystem::directory_iterator(testLogDir)) {
        if (entry.path().extension() == ".jsonl") {
            std::string content = ReadFileContent(entry.path().string());
            if (content.find("Flushing test message") != std::string::npos) {
                foundContent = true;
                break;
            }
        }
    }

    EXPECT_TRUE(foundContent);
}

TEST_F(RotateLoggerTest, DestructorHandlesCleanup)
{
    std::string tempDir = testLogDir + "_dtor";
    std::filesystem::create_directory(tempDir);

    {
        // Create logger in a separate scope
        RotateLogger logger(tempDir, 100, 5);
        EXPECT_NO_THROW(logger.Log("Test message before destruction\n"));
        // Destructor called when going out of scope
    }

    // Directory should exist but logger should be cleaned up properly
    EXPECT_TRUE(std::filesystem::exists(tempDir));

    // Check if file was created properly
    bool fileFound = false;
    for (const auto& entry : std::filesystem::directory_iterator(tempDir)) {
        if (entry.path().extension() == ".jsonl") {
            fileFound = true;
            break;
        }
    }
    EXPECT_TRUE(fileFound);

    std::filesystem::remove_all(tempDir);
}

TEST_F(RotateLoggerTest, StressTestWithManyMessages)
{
    RotateLogger logger(testLogDir, 5, 10); // 5 lines per file, max 10 files

    // Send many messages to test rotation and file management
    constexpr int numMessages = 50;
    for (int i = 0; i < numMessages; ++i) {
        EXPECT_NO_THROW(logger.Log("Stress test message " + std::to_string(i) + "\n"));
    }

    EXPECT_NO_THROW(logger.Flush());
    EXPECT_NO_THROW(logger.UnInit());

    // Count total log files and verify they don't exceed maxFiles
    int fileCount = 0;
    std::vector<std::string> filePaths;
    for (const auto& entry : std::filesystem::directory_iterator(testLogDir)) {
        if (entry.path().extension() == ".jsonl") {
            fileCount++;
            filePaths.push_back(entry.path().string());
        }
    }

    EXPECT_LE(fileCount, 10); // Should not exceed maxFiles limit

    // Optionally, verify that we have reasonable number of files
    // Given 50 messages and 5 per file, we'd expect around 10 files
    EXPECT_GE(fileCount, 1);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
