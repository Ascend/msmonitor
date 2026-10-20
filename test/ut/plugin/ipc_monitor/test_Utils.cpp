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
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "plugin/ipc_monitor/utils/utils.h"

using namespace dynolog_npu::ipc_monitor;

namespace
{

class TempDir
{
   public:
    TempDir()
    {
        std::string tmpl = "/tmp/msmonitor_utils_XXXXXX";
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

TEST(UtilsTest, StringConverters)
{
    uint32_t u = 0;
    EXPECT_FALSE(Str2Uint32(u, ""));
    EXPECT_FALSE(Str2Uint32(u, "12a"));
    EXPECT_FALSE(Str2Uint32(u, "999999999999999999999"));
    EXPECT_TRUE(Str2Uint32(u, "42"));
    EXPECT_EQ(u, 42u);

    float f = 0.0f;
    EXPECT_FALSE(Str2Float(f, ""));
    EXPECT_FALSE(Str2Float(f, "1.2.3"));
    EXPECT_FALSE(Str2Float(f, "."));
    EXPECT_TRUE(Str2Float(f, "-1.5"));
    EXPECT_FLOAT_EQ(f, -1.5f);

    int32_t n = 0;
    EXPECT_FALSE(Str2Int32(n, ""));
    EXPECT_FALSE(Str2Int32(n, "3x"));
    EXPECT_FALSE(Str2Int32(n, "999999999999999999999"));
    EXPECT_TRUE(Str2Int32(n, "-7"));
    EXPECT_EQ(n, -7);

    bool b = false;
    EXPECT_TRUE(Str2Bool(b, "TRUE"));
    EXPECT_TRUE(b);
    EXPECT_TRUE(Str2Bool(b, "0"));
    EXPECT_FALSE(b);
    EXPECT_TRUE(Str2Bool(b, "1"));
    EXPECT_FALSE(Str2Bool(b, "yes"));

    std::string blank = "  ab  ";
    EXPECT_EQ(trim(blank), "ab");
    std::string empty;
    EXPECT_TRUE(trim(empty).empty());
    EXPECT_TRUE(split("", ',').empty());
    auto parts = split(" a, ,b ", ',');
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[0], "a");
    EXPECT_TRUE(parts[1].empty());
    EXPECT_EQ(parts[2], "b");
    auto skipped = split("a,,b", ',');
    ASSERT_EQ(skipped.size(), 2u);
    EXPECT_EQ(join(skipped, "-"), "a-b");
    EXPECT_TRUE(join({}, "-").empty());
    EXPECT_EQ(join({"only"}, ","), "only");
}

TEST(UtilsTest, IdsTimestampMallocAndTypeName)
{
    EXPECT_GT(GetProcessId(), 0);
    EXPECT_FALSE(getCurrentTimestamp().empty());
    EXPECT_GT(getCurrentTimestamp64(), 0u);
    EXPECT_EQ(GenerateUuidV4().size(), 36u);
    EXPECT_FALSE(GetPids().empty());
    auto parent = GetParentPidAndCommand(GetProcessId());
    EXPECT_GT(parent.first, 0);
    EXPECT_FALSE(GetPidCommandPairsofAncestors().empty());
    EXPECT_NE(CalcHashId("abc"), CalcHashId("xyz"));
    EXPECT_FALSE(GetHostName().empty());
    (void)GetHostUid();
    EXPECT_FALSE(GetCurrentUserHomePath().empty());
    EXPECT_FALSE(formatErrorCode(SubModule::IPC, ErrCode::VALUE).empty());
    EXPECT_FALSE(IPC_ERROR(ErrCode::MEMORY).empty());
    EXPECT_EQ(GetCommunicationDataTypeName(MSPTI_ACTIVITY_COMMUNICATION_FP16), "FP16");
    EXPECT_EQ(GetCommunicationDataTypeName(static_cast<msptiCommunicationDataType>(1)), "INT16");
    EXPECT_EQ(GetCommunicationDataTypeName(static_cast<msptiCommunicationDataType>(999)), "INVALID_TYPE");

    auto *ptr = static_cast<uint8_t *>(MsptiMalloc(8, 8));
    ASSERT_NE(ptr, nullptr);
    MsptiFree(ptr);
    MsptiFree(nullptr);
    EXPECT_EQ(MsptiMalloc(8, 0), nullptr);
}

TEST(UtilsTest, PathHelpers)
{
    TempDir dir;
    ASSERT_FALSE(dir.Path().empty());
    const std::string longPath(PATH_MAX + 1, 'p');
    EXPECT_FALSE(PathUtils::IsFileExist(""));
    EXPECT_FALSE(PathUtils::IsFileExist(longPath));
    EXPECT_FALSE(PathUtils::IsFileWritable(""));
    EXPECT_FALSE(PathUtils::IsDir(""));
    EXPECT_FALSE(PathUtils::CreateDir(""));
    EXPECT_TRUE(PathUtils::RealPath("").empty());
    EXPECT_TRUE(PathUtils::RealPath("/no/such/msmonitor_ut_path").empty());
    EXPECT_TRUE(PathUtils::RelativeToAbsPath("").empty());
    EXPECT_TRUE(PathUtils::DirName("").empty());
    EXPECT_FALSE(PathUtils::CreateFile(""));
    EXPECT_FALSE(PathUtils::IsSoftLink(""));
    EXPECT_FALSE(PathUtils::DirPathCheck(""));
    EXPECT_FALSE(PathUtils::IsOwner(""));
    EXPECT_FALSE(PathUtils::IsOwner("/no/such/msmonitor_ut_path"));

    EXPECT_TRUE(PathUtils::IsDir(dir.Path()));
    EXPECT_TRUE(PathUtils::IsFileExist(dir.Path()));
    EXPECT_TRUE(PathUtils::CreateDir(dir.Path()));
    EXPECT_FALSE(PathUtils::RealPath(dir.Path()).empty());
    EXPECT_EQ(PathUtils::RelativeToAbsPath(dir.Path()), dir.Path());
    EXPECT_FALSE(PathUtils::RelativeToAbsPath("msmonitor_rel_ut").empty());
    EXPECT_FALSE(PathUtils::DirName(dir.Path() + "/a").empty());

    const std::string file = dir.Path() + "/a.txt";
    ASSERT_TRUE(PathUtils::CreateFile(file));
    EXPECT_TRUE(PathUtils::IsFileExist(file));
    EXPECT_TRUE(PathUtils::IsFileWritable(file));
    EXPECT_FALSE(PathUtils::IsDir(file));
    EXPECT_FALSE(PathUtils::IsSoftLink(file));
    EXPECT_TRUE(PathUtils::IsOwner(file));
    EXPECT_FALSE(PathUtils::DirPathCheck(file));

    const std::string nested = dir.Path() + "/sub/dir";
    EXPECT_TRUE(PathUtils::CreateDir(nested));
    EXPECT_TRUE(PathUtils::DirPathCheck(nested));
    const std::string link = dir.Path() + "/link";
    ASSERT_EQ(symlink(nested.c_str(), link.c_str()), 0);
    EXPECT_TRUE(PathUtils::IsSoftLink(link));
    EXPECT_TRUE(PathUtils::DirPathCheck(link));

    std::string created;
    const std::string logDir = dir.Path() + "/logs";
    ASSERT_EQ(setenv("MSMONITOR_LOG_PATH", logDir.c_str(), 1), 0);
    EXPECT_TRUE(CreateMsmonitorLogPath(created));
    EXPECT_FALSE(created.empty());
    unsetenv("MSMONITOR_LOG_PATH");
    std::string fromCwd;
    EXPECT_TRUE(CreateMsmonitorLogPath(fromCwd));
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
