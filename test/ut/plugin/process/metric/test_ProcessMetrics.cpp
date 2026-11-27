// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#include <glog/logging.h>
#include <gtest/gtest.h>

#include <limits>

#include "metric/StepMetricExporter.h"

namespace msmonitor::process
{
namespace
{
TEST(ProcessMetrics, SummarisesUnsortedDurations)
{
    std::vector<double> durations{4, 1, 3, 2};
    const auto result = SummariseDurations(durations);
    EXPECT_EQ(result.count, 4U);
    EXPECT_DOUBLE_EQ(result.total, 10);
    EXPECT_DOUBLE_EQ(result.mean, 2.5);
    EXPECT_DOUBLE_EQ(result.minimum, 1);
    EXPECT_DOUBLE_EQ(result.maximum, 4);
    EXPECT_DOUBLE_EQ(result.p50, 2.5);
    EXPECT_DOUBLE_EQ(result.p99, 3.97);
}

TEST(ProcessMetrics, SeparatesTwoDurationClustersAndFiltersInvalidValues)
{
    std::vector<double> durations(20, 100e-6);
    durations.insert(durations.end(), 20, 1e-6);
    durations.insert(durations.end(), {0, -1, std::numeric_limits<double>::infinity()});
    const auto clusters = ClusterDurations(durations);
    ASSERT_EQ(clusters.size(), 2U);
    EXPECT_EQ(clusters[0].count, 20U);
    EXPECT_DOUBLE_EQ(clusters[0].p50, 1e-6);
    EXPECT_EQ(clusters[1].count, 20U);
    EXPECT_DOUBLE_EQ(clusters[1].p99, 100e-6);
}

TEST(ProcessMetrics, UsesOwnedEventsAndGroupsByOperationAndStream)
{
    StepDataOutput step;
    step.rank = 2;
    step.boundary = {7, 100, 300};
    step.completion = StepCompletion::QuietBatch;
    const auto kernel = [](const char *name, unsigned stream)
    {
        return std::make_shared<StepEvent>(
            StepEvent{120, 140, {{"kind", "Kernel"}, {"name", name}, {"deviceId", 0}, {"streamId", stream}}});
    };
    step.ownedEvents = {kernel("Add_hash1", 2), kernel("Add_hash2", 2), kernel("Add_hash1", 3),
                        kernel("AddN_hash1", 2)};
    step.ownedEvents.push_back(std::make_shared<StepEvent>(
        StepEvent{150, 190, {{"kind", "Marker"}, {"sourceKind", "Host"}, {"domain", "custom"}, {"name", "forward"}}}));
    step.events = step.ownedEvents;
    step.events.push_back(kernel("Other_hash", 2));
    StepSummary summary;
    StepMetricExporter exporter(
        {}, [](const StepMetricData &) {}, [&](StepSummary value) { summary = std::move(value); });
    exporter.ExportStep(step);

    EXPECT_TRUE(summary.valid);
    EXPECT_EQ(summary.rank, 2);
    EXPECT_EQ(summary.stepId, 7U);
    ASSERT_EQ(summary.operators.size(), 3U);
    EXPECT_EQ(summary.operators.at({0, 2, "Kernel", "Add"}).count, 2U);
    EXPECT_EQ(summary.operators.at({0, 3, "Kernel", "Add"}).count, 1U);
    EXPECT_EQ(summary.operators.at({0, 2, "Kernel", "AddN"}).count, 1U);
    const auto &range = summary.ranges.at({"Host", "custom", "forward"});
    EXPECT_EQ(range.count, 1U);
    EXPECT_DOUBLE_EQ(range.sum, 40.0 / MetricConstants::NsPerSecond);
}

TEST(ProcessMetrics, InvalidatesIncompleteAndOverBudgetSummaries)
{
    StepDataOutput step;
    step.rank = 0;
    step.boundary = {1, 100, 200};
    const auto event = std::make_shared<StepEvent>(
        StepEvent{110, 120, {{"kind", "Kernel"}, {"name", "Add"}, {"deviceId", 0}, {"streamId", 0}}});
    step.events = step.ownedEvents = {event, event};

    StepSummary summary;
    MetricOptions options;
    options.maxSamplesPerGroup = 1;
    StepMetricExporter exporter(
        options, [](const StepMetricData &) {}, [&](StepSummary value) { summary = std::move(value); });
    step.completion = StepCompletion::QuietBatch;
    exporter.ExportStep(step);
    EXPECT_TRUE(summary.complete);
    EXPECT_FALSE(summary.valid);
    EXPECT_TRUE(summary.operators.empty());
    EXPECT_TRUE(summary.kernels.empty());

    step.ownedEvents.pop_back();
    step.completion = StepCompletion::Stopped;
    exporter.ExportStep(step);
    EXPECT_FALSE(summary.complete);
    EXPECT_FALSE(summary.valid);
    EXPECT_TRUE(summary.operators.empty());
}
}  // namespace
}  // namespace msmonitor::process

int main(int argc, char **argv)
{
    google::InitGoogleLogging(argv[0]);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
