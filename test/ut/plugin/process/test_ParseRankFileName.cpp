// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>

#include "ProcessRunnerDetail.h"

namespace msmonitor::process_runner::detail
{
namespace
{

TEST(ParseRankFileName, AcceptsNormalName)
{
    RankFile parsed;
    ASSERT_TRUE(ParseRankFileName(std::filesystem::path("msmonitor_12345_20260101010101012_7.jsonl"), parsed));
    EXPECT_EQ(parsed.rank, 7);
    EXPECT_EQ(parsed.processId, 12345U);
    EXPECT_EQ(parsed.timestamp, 20260101010101012ULL);
}

TEST(ParseRankFileName, UsesFileNameOnly)
{
    RankFile parsed;
    ASSERT_TRUE(ParseRankFileName(std::filesystem::path("/var/log/msmonitor_12345_20260101010101012_0.jsonl"), parsed));
    EXPECT_EQ(parsed.rank, 0);
    EXPECT_EQ(parsed.processId, 12345U);
}

TEST(ParseRankFileName, RejectsWrongPrefixOrSuffix)
{
    RankFile parsed;
    EXPECT_FALSE(ParseRankFileName(std::filesystem::path("other_12345_20260101010101012_7.jsonl"), parsed));
    EXPECT_FALSE(ParseRankFileName(std::filesystem::path("msmonitor_12345_20260101010101012_7.json"), parsed));
    EXPECT_FALSE(ParseRankFileName(std::filesystem::path("msmonitor_.jsonl"), parsed));
}

TEST(ParseRankFileName, RejectsMissingSeparator)
{
    RankFile parsed;
    EXPECT_FALSE(ParseRankFileName(std::filesystem::path("msmonitor_12345.jsonl"), parsed));
}

TEST(ParseRankFileName, RejectsNonNumericProcessId)
{
    RankFile parsed;
    // 只消费了前两位数字时必须拒绝，否则会被当成 pid=12
    EXPECT_FALSE(ParseRankFileName(std::filesystem::path("msmonitor_12x_20260101010101012_7.jsonl"), parsed));
    EXPECT_FALSE(ParseRankFileName(std::filesystem::path("msmonitor_0_20260101010101012_7.jsonl"), parsed));
}

TEST(ParseRankFileName, DegradesWhenTimestampInvalid)
{
    RankFile parsed;
    ASSERT_TRUE(ParseRankFileName(std::filesystem::path("msmonitor_12345_abc_7.jsonl"), parsed));
    EXPECT_EQ(parsed.rank, 7);
    EXPECT_EQ(parsed.processId, 12345U);
    EXPECT_EQ(parsed.timestamp, 0U);
}

TEST(ParseRankFileName, RejectsInvalidRank)
{
    RankFile parsed;
    EXPECT_FALSE(ParseRankFileName(std::filesystem::path("msmonitor_12345_20260101010101012_abc.jsonl"), parsed));
    EXPECT_FALSE(ParseRankFileName(std::filesystem::path("msmonitor_12345_20260101010101012_-1.jsonl"), parsed));
    // rank 尾部残留字符不能被静默忽略
    EXPECT_FALSE(ParseRankFileName(std::filesystem::path("msmonitor_12345_20260101010101012_7x.jsonl"), parsed));
}

}  // namespace
}  // namespace msmonitor::process_runner::detail

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
