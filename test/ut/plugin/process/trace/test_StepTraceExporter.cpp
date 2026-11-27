// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#include <glog/logging.h>
#include <gtest/gtest.h>
#include <sqlite3.h>

#include <cstdlib>
#include <fstream>

#include "trace/StepTraceExporter.h"

namespace msmonitor::process
{
namespace
{
class StepTraceExporterTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        auto path = (std::filesystem::temp_directory_path() / "msmonitor-trace-XXXXXX").string();
        const auto *created = mkdtemp(path.data());
        ASSERT_NE(created, nullptr);
        outputDir = created;
    }

    void TearDown() override
    {
        if (!outputDir.empty())
        {
            std::filesystem::remove_all(outputDir);
        }
    }

    std::filesystem::path outputDir;
};

TEST_F(StepTraceExporterTest, PublishesFullTraceBeforeFlushAndRetainsNewestFiles)
{
    TraceOptions traceOptions;
    traceOptions.outputDir = outputDir;
    traceOptions.stepsPerFile = 2;
    traceOptions.keepFiles = 1;
    StepTraceExporter exporter(traceOptions);
    const auto output = traceOptions.outputDir / "rank_2";
    const auto crossing =
        std::make_shared<StepEvent>(StepEvent{1500, 2500, {{"kind", "Marker"}, {"name", "crossing"}}});
    const auto writeStep = [&](std::uint64_t id)
    {
        auto step = std::make_shared<StepDataOutput>();
        step->rank = 2;
        step->boundary = {id, id * 1000, (id + 1) * 1000};
        if (id < 3)
        {
            step->events.push_back(crossing);
        }
        exporter.WriteStep(std::move(step));
    };
    writeStep(1);
    EXPECT_FALSE(std::filesystem::exists(output));
    writeStep(2);
    std::ifstream file(output / "step_1_2" / "trace.json");
    ASSERT_TRUE(file.is_open());
    const auto trace = nlohmann::json::parse(file);
    std::size_t crossingCount = 0;
    for (const auto &event : trace.at("traceEvents"))
    {
        crossingCount += event.value("name", "") == "crossing";
    }
    EXPECT_EQ(crossingCount, 1U);
    writeStep(3);
    EXPECT_FALSE(std::filesystem::exists(output / "step_3"));
    exporter.Flush();
    EXPECT_TRUE(std::filesystem::exists(output / "step_3" / "trace.json"));
    EXPECT_FALSE(std::filesystem::exists(output / "step_1_2"));
}

TEST_F(StepTraceExporterTest, WritesMindStudioDbWithStepAndDeviceEvents)
{
    TraceOptions options;
    options.outputDir = outputDir;
    options.format = TraceFormat::MindStudioDb;
    StepTraceExporter exporter(options);
    auto step = std::make_shared<StepDataOutput>();
    step->rank = 2;
    step->boundary = {7, 100, 200};
    step->events.push_back(std::make_shared<StepEvent>(StepEvent{115, 125, {{"kind", "FutureActivity"}}}));
    step->events.push_back(std::make_shared<StepEvent>(
        StepEvent{120, 180, {{"kind", "Marker"}, {"sourceKind", "Device"}, {"deviceId", 0}, {"streamId", 3}}}));
    exporter.WriteStep(step);

    const auto path = outputDir / "rank_2" / "step_7" / "trace.db";
    sqlite3 *handle = nullptr;
    const int status = sqlite3_open_v2(path.c_str(), &handle, SQLITE_OPEN_READONLY, nullptr);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(handle, sqlite3_close);
    ASSERT_EQ(status, SQLITE_OK);
    const char *query =
        "SELECT CAST(value AS INTEGER), startTimeNs, endTimeNs, TASK.startNs, TASK.endNs, streamId "
        "FROM META_DATA, SESSION_TIME_INFO, TASK, RANK_DEVICE_MAP "
        "WHERE name = 'msmonitor_step_id' AND rankId = 2 AND RANK_DEVICE_MAP.deviceId = TASK.deviceId";
    sqlite3_stmt *raw = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(db.get(), query, -1, &raw, nullptr), SQLITE_OK);
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(raw, sqlite3_finalize);
    ASSERT_EQ(sqlite3_step(statement.get()), SQLITE_ROW);
    const int expected[] = {7, 100, 200, 120, 180, 3};
    for (int i = 0; i < 6; ++i)
    {
        EXPECT_EQ(sqlite3_column_int(statement.get(), i), expected[i]);
    }
    EXPECT_EQ(sqlite3_step(statement.get()), SQLITE_DONE);
}
}  // namespace
}  // namespace msmonitor::process

int main(int argc, char **argv)
{
    google::InitGoogleLogging(argv[0]);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
