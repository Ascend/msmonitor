/*
 * -------------------------------------------------------------------------
 * This file is part of the MindStudio project.
 * Copyright (c) 2026 Huawei Technologies Co.,Ltd.
 *
 * MindStudio is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the
 * Mulan PSL v2.
 *
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
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "dynolog/src/utils.h"

namespace dynolog
{
namespace
{

constexpr int kMaxJsonDepth = 10;

std::string NestedObjects(int depth)
{
    std::string json;
    for (int i = 0; i < depth; ++i)
    {
        json += "{\"a\":";
    }
    json += "1";
    for (int i = 0; i < depth; ++i)
    {
        json += "}";
    }
    return json;
}

TEST(CheckJsonDepthTest, AcceptsUniqueKeys)
{
    EXPECT_TRUE(CheckJsonDepth(R"({"fn":"getStatus","job_id":1,"pids":[1,2]})"));
}

TEST(CheckJsonDepthTest, RejectsDuplicateTopLevelKey)
{
    EXPECT_FALSE(CheckJsonDepth(R"({"fn":"getStatus","fn":"getVersion"})"));
}

TEST(CheckJsonDepthTest, RejectsDuplicateKeyWithDifferentValueTypes)
{
    EXPECT_FALSE(CheckJsonDepth(R"({"config":"X","config":123})"));
    EXPECT_FALSE(CheckJsonDepth(R"({"config":123,"config":"X"})"));
}

TEST(CheckJsonDepthTest, RejectsDuplicateKeyWrittenWithEscape)
{
    // Both keys decode to "a", so they are duplicates even though the raw
    // message spells them differently.
    EXPECT_FALSE(CheckJsonDepth(R"({"a":1,"\u0061":2})"));
}

TEST(CheckJsonDepthTest, RejectsDuplicateNestedKey)
{
    EXPECT_FALSE(CheckJsonDepth(R"({"x":{"a":1,"a":2}})"));
    EXPECT_FALSE(CheckJsonDepth(R"({"x":[{"a":1,"a":2}]})"));
}

TEST(CheckJsonDepthTest, AcceptsSameKeyInSiblingObjects)
{
    EXPECT_TRUE(CheckJsonDepth(R"({"x":{"a":1},"y":{"a":2}})"));
    EXPECT_TRUE(CheckJsonDepth(R"({"x":[{"a":1},{"a":2}]})"));
}

TEST(CheckJsonDepthTest, RejectsDuplicateKeyWithControlCharacter)
{
    EXPECT_FALSE(CheckJsonDepth("{\"a\\nb\":1,\"a\\nb\":2}"));
}

TEST(CheckJsonDepthTest, EnforcesMaximumDepth)
{
    EXPECT_TRUE(CheckJsonDepth(NestedObjects(kMaxJsonDepth)));
    EXPECT_FALSE(CheckJsonDepth(NestedObjects(kMaxJsonDepth + 1)));
}

TEST(CheckJsonDepthTest, RejectsMalformedJson) { EXPECT_FALSE(CheckJsonDepth("{")); }

class PathUtilsTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        const ::testing::TestInfo *info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir_ = std::filesystem::temp_directory_path() /
               ("dynolog_utils_ut_" + std::to_string(::getpid()) + "_" + info->name());
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
        ASSERT_TRUE(std::filesystem::create_directories(dir_, ec)) << ec.message();

        file_ = dir_ / "file.txt";
        std::ofstream out(file_);
        out << "x";
        out.close();
        ASSERT_TRUE(std::filesystem::exists(file_));

        link_ = dir_ / "link";
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }

    std::filesystem::path dir_;
    std::filesystem::path file_;
    std::filesystem::path link_;
};

TEST(PathUtilsAccessTest, AccessHandlesEmptyAndMissingPaths)
{
    EXPECT_FALSE(PathUtils::Access("", F_OK));
    EXPECT_FALSE(PathUtils::Exist(""));
    EXPECT_FALSE(PathUtils::Access("/definitely/not/here/dynolog_ut", F_OK));
    EXPECT_TRUE(PathUtils::Access("/", F_OK));
}

TEST(PathUtilsAccessTest, ExistReportsExistingPaths)
{
    EXPECT_TRUE(PathUtils::Exist("/"));
    EXPECT_FALSE(PathUtils::Exist("/definitely/not/here/dynolog_ut"));
}

TEST_F(PathUtilsTest, AccessAcceptsReadableDirectory)
{
    EXPECT_TRUE(PathUtils::Exist(dir_.string()));
    EXPECT_TRUE(PathUtils::Access(dir_.string(), F_OK));
    EXPECT_TRUE(PathUtils::Access(dir_.string(), R_OK | X_OK));
}

TEST_F(PathUtilsTest, IsDirectoryDistinguishesFilesAndMissingPaths)
{
    EXPECT_TRUE(PathUtils::IsDirectory(dir_.string()));
    EXPECT_FALSE(PathUtils::IsDirectory(file_.string()));
    EXPECT_FALSE(PathUtils::IsDirectory((dir_ / "missing").string()));
    EXPECT_FALSE(PathUtils::IsDirectory(""));
}

TEST_F(PathUtilsTest, IsFileDistinguishesFilesAndDirectories)
{
    EXPECT_TRUE(PathUtils::IsFile(file_.string()));
    EXPECT_FALSE(PathUtils::IsFile(dir_.string()));
    EXPECT_FALSE(PathUtils::IsFile((dir_ / "missing").string()));
    EXPECT_FALSE(PathUtils::IsFile(""));
}

TEST_F(PathUtilsTest, IsSoftLinkDetectsSymlinks)
{
    std::error_code ec;
    std::filesystem::create_symlink(file_, link_, ec);
    if (ec)
    {
        GTEST_SKIP() << "symlinks are not supported: " << ec.message();
    }

    EXPECT_TRUE(PathUtils::IsSoftLink(link_.string()));
    EXPECT_FALSE(PathUtils::IsSoftLink(file_.string()));
    EXPECT_FALSE(PathUtils::IsSoftLink((dir_ / "missing").string()));
    EXPECT_FALSE(PathUtils::IsSoftLink(""));
}

TEST_F(PathUtilsTest, IsWritableByOthersChecksGroupAndOtherWriteBits)
{
    ASSERT_EQ(::chmod(file_.c_str(), 0644), 0);
    EXPECT_FALSE(PathUtils::IsWritableByOthers(file_.string()));

    ASSERT_EQ(::chmod(file_.c_str(), 0666), 0);
    EXPECT_TRUE(PathUtils::IsWritableByOthers(file_.string()));

    // A failed stat is reported as unsafe so callers fail closed.
    EXPECT_TRUE(PathUtils::IsWritableByOthers((dir_ / "missing").string()));
}

TEST_F(PathUtilsTest, IsOwnerChecksCurrentUser)
{
    EXPECT_TRUE(PathUtils::IsOwner(file_.string()));
    EXPECT_FALSE(PathUtils::IsOwner((dir_ / "missing").string()));

    // "/" belongs to root, so it is not owned by the test user.
    if (::getuid() != 0)
    {
        EXPECT_FALSE(PathUtils::IsOwner("/"));
    }
}

TEST_F(PathUtilsTest, GetAbsolutePathNormalizesPaths)
{
    EXPECT_EQ(PathUtils::GetAbsolutePath("/"), "/");
    EXPECT_EQ(PathUtils::GetAbsolutePath(dir_.string()), dir_.string());
    // A trailing slash is removed for everything except the root directory.
    EXPECT_EQ(PathUtils::GetAbsolutePath(dir_.string() + "/"), dir_.string());
    // Lexical normalization collapses "." and ".." without touching the disk.
    EXPECT_EQ(PathUtils::GetAbsolutePath((dir_ / "a" / "." / "..").string()), dir_.string());

    const std::string relative = PathUtils::GetAbsolutePath("dynolog_ut_relative");
    EXPECT_TRUE(std::filesystem::path(relative).is_absolute());
    EXPECT_EQ(std::filesystem::path(relative).filename().string(), "dynolog_ut_relative");
}

TEST_F(PathUtilsTest, DirPathCheckValidatesDirectories)
{
    EXPECT_TRUE(PathUtils::DirPathCheck(dir_.string()));
    EXPECT_FALSE(PathUtils::DirPathCheck(file_.string()));
    EXPECT_FALSE(PathUtils::DirPathCheck((dir_ / "missing").string()));
    EXPECT_FALSE(PathUtils::DirPathCheck(""));
    EXPECT_FALSE(PathUtils::DirPathCheck(std::string(PATH_MAX + 1, 'a')));

    // A symlink to a directory is accepted (a warning is logged).
    std::error_code ec;
    std::filesystem::create_directory_symlink(dir_, link_, ec);
    if (!ec)
    {
        EXPECT_TRUE(PathUtils::DirPathCheck(link_.string()));
    }
}

TEST(DynologSystemUtilsTest, IsRootMatchesCurrentUid) { EXPECT_EQ(IsRoot(), ::getuid() == 0); }

TEST(DynologSystemUtilsTest, GetCurrentUserHomePathMatchesPasswdEntry)
{
    const std::string home = GetCurrentUserHomePath();
    EXPECT_EQ(home, GetCurrentUserHomePath());

    struct passwd *pw = ::getpwuid(::getuid());
    if (pw != nullptr && pw->pw_dir != nullptr)
    {
        EXPECT_EQ(home, std::string(pw->pw_dir));
    }
    else
    {
        EXPECT_TRUE(home.empty());
    }
}

TEST(DynologSystemUtilsTest, GetHostUidReturnsStableDecimalString)
{
    const std::string uid = GetHostUid();
    EXPECT_EQ(uid, GetHostUid());
    if (uid.empty())
    {
        // Hosts without any non-loopback interface have no MAC address.
        return;
    }
    EXPECT_TRUE(std::all_of(uid.begin(), uid.end(), [](unsigned char c) { return std::isdigit(c) != 0; }));
    EXPECT_LE(uid.size(), 20U);
}

}  // namespace
}  // namespace dynolog

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
