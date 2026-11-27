// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#pragma once

#include "ProcessStages.h"

namespace msmonitor::process
{

// One rank-local analyser. By default, device MSTX ranges named "step N" define step boundaries.
// Selected step ranges may overlap; events are shared by every intersecting step.
class StepDataAnalyser final : public StepAnalyser
{
   public:
    explicit StepDataAnalyser(Options options);
    StepDataAnalyser(Options options, StepMarkerDecoder decodeStep,
                     std::function<void(const nlohmann::json &)> metadataSink = {});
    ~StepDataAnalyser() override;
    StepDataAnalyser(const StepDataAnalyser &) = delete;
    StepDataAnalyser &operator=(const StepDataAnalyser &) = delete;

    // Buffer timed records without publishing from inside a read batch.
    void ProcessLine(const JsonlLine &line) override;
    // A step needs a later, nonempty, partial-free batch with no intersecting event.
    // This bounds tolerated lateness; records arriving after publication fail explicitly.
    void EndBatch(const ReadBatch &batch, const EmitStepData &emit) override;
    // Only a stable, fully read offline input may use inputComplete=true.
    void Finish(bool inputComplete, const EmitStepData &emit) override;
    std::int64_t Rank() const override;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace msmonitor::process
