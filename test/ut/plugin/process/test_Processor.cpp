// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#include <glog/logging.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <future>
#include <iterator>
#include <map>
#include <stdexcept>
#include <thread>
#include <vector>

#include "Processor.h"
#include "trace/StepTraceExporter.h"

namespace msmonitor::process
{
namespace
{
using namespace std::chrono_literals;

// Turn each complete line into one synthetic step. The real analyser is added separately.
class RecordingAnalyser final : public StepAnalyser
{
   public:
    std::int64_t Rank() const override { return rank; }
    void ProcessLine(const JsonlLine &line) override
    {
        lines.push_back(line.text);
        if (onLine)
        {
            onLine();
        }
    }

    void EndBatch(const ReadBatch &batch, const EmitStepData &emit) override
    {
        PublishNewLines(emit);
        if (onBatch)
        {
            onBatch(batch);
        }
    }

    void Finish(bool inputComplete, const EmitStepData &emit) override
    {
        complete = inputComplete;
        PublishNewLines(emit);
    }

    std::vector<std::string> lines;
    std::int64_t rank{2};
    std::function<void()> onLine;
    std::function<void(const ReadBatch &)> onBatch;
    std::optional<bool> complete;

   private:
    void PublishNewLines(const EmitStepData &emit)
    {
        while (published_ < lines.size())
        {
            auto step = std::make_shared<StepDataOutput>();
            step->boundary = {++published_, 1, 2};
            step->rank = Rank();
            emit(std::move(step));
        }
    }

    std::uint64_t published_{0};
};

class RecordingMetrics final : public MetricExporter
{
   public:
    void ExportStep(const StepDataOutput &step) override
    {
        if (onExport)
        {
            onExport(step);
        }
        steps.push_back(step.boundary.stepId);
    }

    std::function<void(const StepDataOutput &)> onExport;
    std::vector<std::uint64_t> steps;
};

class RecordingTrace final : public TraceExporter
{
   public:
    void WriteStep(StepDataPtr step) override
    {
        steps.push_back(step->boundary.stepId);
        if (onWrite)
        {
            onWrite();
        }
    }

    std::function<void()> onWrite;
    std::vector<std::uint64_t> steps;
};

class ProcessorTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        auto path = (std::filesystem::temp_directory_path() / "msmonitor-process-XXXXXX").string();
        const auto *created = mkdtemp(path.data());
        ASSERT_NE(created, nullptr);
        options.inputDir = created;
        options.rank = analyser.Rank();
        options.readChunkBytes = 8;
        options.queueCapacity = 1;
        options.pollInterval = 2ms;
    }

    void TearDown() override
    {
        if (!options.inputDir.empty())
        {
            std::filesystem::remove_all(options.inputDir);
        }
    }

    void Append(const std::string &name, const std::string &content)
    {
        std::ofstream file;
        file.exceptions(std::ios::failbit | std::ios::badbit);
        file.open(options.inputDir / name, std::ios::binary | std::ios::app);
        file << content;
        file.close();
    }

    Options options;
    RecordingAnalyser analyser;
    RecordingMetrics metrics;
    RecordingTrace trace;
};

TEST_F(ProcessorTest, ReadsSelectedRankInOrderAndDrainsBothExporters)
{
    options.rank = analyser.rank = ProcessConstants::Unranked;
    options.processId = 10;
    Append("msmonitor_10_100_-1.jsonl", "{\"id\":1}\n{\"id\":2}\r\n");
    Append("msmonitor_10_200_-1.jsonl", "{\"id\":3}\n");
    Append("msmonitor_11_100_-1.jsonl", "other process\n");
    Append("msmonitor_10_100_3.jsonl", "other rank\n");

    // A metric blocked on the first step must not prevent trace export of that same step.
    std::promise<void> firstTrace;
    auto traceReady = firstTrace.get_future();
    trace.onWrite = [&]
    {
        if (trace.steps.size() == 1)
        {
            firstTrace.set_value();
        }
    };
    metrics.onExport = [&](const StepDataOutput &step)
    {
        if (step.boundary.stepId == 1 && traceReady.wait_for(3s) != std::future_status::ready)
        {
            throw std::runtime_error("trace export did not run concurrently");
        }
    };

    Processor processor(options, analyser, metrics, trace);
    analyser.onBatch = [&](const ReadBatch &) { processor.RequestStop(); };
    processor.Run();

    EXPECT_EQ(analyser.complete, false);
    EXPECT_EQ(analyser.lines, (std::vector<std::string>{"{\"id\":1}", "{\"id\":2}", "{\"id\":3}"}));
    EXPECT_EQ(metrics.steps, (std::vector<std::uint64_t>{1, 2, 3}));
    EXPECT_EQ(trace.steps, metrics.steps);
}

TEST_F(ProcessorTest, RejectsNullStep)
{
    class NullStepAnalyser final : public StepAnalyser
    {
       public:
        std::int64_t Rank() const override { return 2; }
        void ProcessLine(const JsonlLine &) override {}
        void EndBatch(const ReadBatch &, const EmitStepData &emit) override { emit({}); }
        void Finish(bool, const EmitStepData &) override {}
    } nullAnalyser;
    Processor processor(options, nullAnalyser, metrics, trace);
    EXPECT_THROW(processor.Run(), std::invalid_argument);
}

TEST_F(ProcessorTest, GroupsTracesAndFlushesTailWhileExportingEachMetric)
{
    Append("msmonitor_10_100_2.jsonl", "{}\n{}\n{}\n{}\n{}\n");
    TraceOptions traceOptions;
    traceOptions.outputDir = options.inputDir / "traces";
    traceOptions.stepsPerFile = 2;
    StepTraceExporter traceExporter(traceOptions);
    Processor processor(options, analyser, metrics, traceExporter);
    analyser.onBatch = [&](const ReadBatch &) { processor.RequestStop(); };
    processor.Run();

    EXPECT_EQ(metrics.steps, (std::vector<std::uint64_t>{1, 2, 3, 4, 5}));
    const auto output = traceOptions.outputDir / "rank_2";
    for (const auto &[directory, expected] :
         std::map<std::string, std::vector<std::uint64_t>>{{"step_1_2", {1, 2}}, {"step_3_4", {3, 4}},
                                                            {"step_5", {5}}})
    {
        std::ifstream file(output / directory / "trace.json");
        ASSERT_TRUE(file.is_open());
        const auto document = nlohmann::json::parse(file);
        std::vector<std::uint64_t> ids;
        for (const auto &step : document.at("metadata").at("steps"))
        {
            ids.push_back(step.at("stepId").get<std::uint64_t>());
        }
        EXPECT_EQ(ids, expected);
        std::vector<std::string> markers;
        for (const auto &event : document.at("traceEvents"))
        {
            if (event.value("cat", "") == "Step")
            {
                markers.push_back(event.at("name").get<std::string>());
            }
        }
        ASSERT_EQ(markers.size(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            EXPECT_EQ(markers[i], "step " + std::to_string(expected[i]));
        }
    }
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(output), std::filesystem::directory_iterator()), 3);
}

TEST_F(ProcessorTest, PublishesFullTraceBeforeFlushAndRetainsNewestFiles)
{
    TraceOptions traceOptions;
    traceOptions.outputDir = options.inputDir / "traces";
    traceOptions.stepsPerFile = 2;
    traceOptions.keepFiles = 1;
    StepTraceExporter exporter(traceOptions);
    const auto output = traceOptions.outputDir / "rank_2";
    const auto crossing = std::make_shared<StepEvent>(
        StepEvent{1500, 2500, {{"kind", "Marker"}, {"name", "crossing"}}});
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

TEST_F(ProcessorTest, FollowsAppendsAndRotatedFilesUntilStopped)
{
    const std::string active = "msmonitor_10_100_2.jsonl";
    Append(active, "{\"id\":");
    Processor processor(options, analyser, metrics, trace);
    unsigned polls = 0;
    analyser.onBatch = [&](const ReadBatch &batch)
    {
        switch (++polls)
        {
            case 1:
                EXPECT_TRUE(batch.hasPartialLine);
                EXPECT_TRUE(analyser.lines.empty());
                Append(active, "1}\n");
                Append("msmonitor_10_200_2.jsonl", "{\"id\":2}\n");
                break;
            case 2:
                EXPECT_TRUE(batch.hasData);
                EXPECT_FALSE(batch.hasPartialLine);
                EXPECT_EQ(analyser.lines.size(), 2U);
                break;
            case 3:
                EXPECT_FALSE(batch.hasData);
                processor.RequestStop();
                break;
        }
    };

    processor.Run();

    EXPECT_EQ(analyser.complete, false);
    EXPECT_EQ(analyser.lines, (std::vector<std::string>{"{\"id\":1}", "{\"id\":2}"}));
    EXPECT_EQ(metrics.steps, (std::vector<std::uint64_t>{1, 2}));
    EXPECT_EQ(trace.steps, metrics.steps);
}

TEST_F(ProcessorTest, PropagatesExporterFailureWithBoundedQueues)
{
    Append("msmonitor_10_100_2.jsonl", "{}\n{}\n{}\n{}\n");
    metrics.onExport = [](const StepDataOutput &) { throw std::runtime_error("metric sink failed"); };
    Processor processor(options, analyser, metrics, trace);

    try
    {
        processor.Run();
        FAIL() << "Expected the worker error";
    }
    catch (const std::runtime_error &error)
    {
        EXPECT_STREQ(error.what(), "metric sink failed");
    }
}

TEST_F(ProcessorTest, StopFromAnotherThreadWakesPollingReader)
{
    options.pollInterval = 3s;
    Append("msmonitor_10_100_2.jsonl", "{}\n");
    Processor processor(options, analyser, metrics, trace);
    std::promise<void> firstBatch;
    auto batchReady = firstBatch.get_future();
    unsigned polls = 0;
    analyser.onBatch = [&](const ReadBatch &)
    {
        if (++polls == 1)
        {
            firstBatch.set_value();
        }
        else
        {
            processor.RequestStop();
        }
    };
    auto run = std::async(std::launch::async, [&] { processor.Run(); });
    const auto batchStatus = batchReady.wait_for(1s);
    if (batchStatus == std::future_status::ready)
    {
        // Give Run time to enter the timed wait after the callback returns.
        std::this_thread::sleep_for(30ms);
    }
    processor.RequestStop();

    EXPECT_EQ(batchStatus, std::future_status::ready);
    EXPECT_EQ(run.wait_for(1s), std::future_status::ready);
    run.get();
    EXPECT_EQ(polls, 1U);
    EXPECT_EQ(metrics.steps, std::vector<std::uint64_t>{1});
    EXPECT_EQ(trace.steps, metrics.steps);
}

TEST_F(ProcessorTest, StopsWithPartialTailAndDrainsCompleteLines)
{
    Append("msmonitor_10_100_2.jsonl", "{}\n{\"id\":");
    Processor processor(options, analyser, metrics, trace);
    analyser.onBatch = [&](const ReadBatch &batch)
    {
        EXPECT_TRUE(batch.hasPartialLine);
        processor.RequestStop();
    };

    processor.Run();

    EXPECT_EQ(analyser.complete, false);
    EXPECT_EQ(analyser.lines, std::vector<std::string>{"{}"});
    EXPECT_EQ(metrics.steps, std::vector<std::uint64_t>{1});
    EXPECT_EQ(trace.steps, metrics.steps);
}

TEST_F(ProcessorTest, ContinuesWhenRotationRemovesAnUnreadFile)
{
    Append("msmonitor_10_100_2.jsonl", "{}\n");
    const std::string removed = "msmonitor_10_200_2.jsonl";
    Append(removed, "removed\n");
    Append("msmonitor_10_300_2.jsonl", "{\"id\":3}\n");
    Processor processor(options, analyser, metrics, trace);
    analyser.onLine = [&]
    {
        if (analyser.lines.size() == 1)
        {
            EXPECT_TRUE(std::filesystem::remove(options.inputDir / removed));
        }
    };
    analyser.onBatch = [&](const ReadBatch &batch)
    {
        EXPECT_TRUE(batch.hasPartialLine);
        processor.RequestStop();
    };

    processor.Run();

    EXPECT_EQ(analyser.lines, (std::vector<std::string>{"{}", "{\"id\":3}"}));
    EXPECT_EQ(metrics.steps, (std::vector<std::uint64_t>{1, 2}));
    EXPECT_EQ(trace.steps, metrics.steps);
}

TEST_F(ProcessorTest, RetiresDeletedCursorWhileKeepingActiveOffset)
{
    const std::string retired = "msmonitor_10_100_2.jsonl";
    const std::string active = "msmonitor_10_200_2.jsonl";
    Append(retired, "{\"old\":1}\n");
    Append(active, "{}\n");
    Processor processor(options, analyser, metrics, trace);
    unsigned polls = 0;
    analyser.onBatch = [&](const ReadBatch &)
    {
        if (++polls == 1)
        {
            EXPECT_TRUE(std::filesystem::remove(options.inputDir / retired));
        }
        else if (polls == 2)
        {
            Append(active, "{}\n");
            Append(retired, "{}\n");
        }
        else
        {
            processor.RequestStop();
        }
    };
    processor.Run();
    EXPECT_EQ(analyser.lines, (std::vector<std::string>{"{\"old\":1}", "{}", "{}", "{}"}));
}

TEST_F(ProcessorTest, RestartsCursorWhenUnreadFileDisappearsAndIsRecreatedInSamePoll)
{
    const std::string earlier = "msmonitor_10_100_2.jsonl";
    const std::string replaced = "msmonitor_10_200_2.jsonl";
    Append(replaced, "{\"id\":1}\n{\"id\":");
    Processor processor(options, analyser, metrics, trace);
    unsigned polls = 0;
    analyser.onBatch = [&](const ReadBatch &batch)
    {
        switch (++polls)
        {
            case 1:
                EXPECT_TRUE(batch.hasPartialLine);
                Append(earlier, "{\"remove\":true}\n");
                Append(replaced, "2}\n");
                Append("msmonitor_10_300_2.jsonl", "{\"recreate\":true}\n");
                break;
            case 2:
                EXPECT_TRUE(batch.hasPartialLine);
                break;
            default:
                EXPECT_FALSE(batch.hasPartialLine);
                processor.RequestStop();
                break;
        }
    };
    analyser.onLine = [&]
    {
        if (analyser.lines.back() == "{\"remove\":true}")
        {
            EXPECT_TRUE(std::filesystem::remove(options.inputDir / replaced));
        }
        else if (analyser.lines.back() == "{\"recreate\":true}")
        {
            Append(replaced, "{\"replacement\":12345}\n");
        }
    };

    processor.Run();

    EXPECT_EQ(analyser.lines, (std::vector<std::string>{"{\"id\":1}", "{\"remove\":true}", "{\"recreate\":true}",
                                                        "{\"replacement\":12345}"}));
}
}  // namespace
}  // namespace msmonitor::process

int main(int argc, char **argv)
{
    google::InitGoogleLogging(argv[0]);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
