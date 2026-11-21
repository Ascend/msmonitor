// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#pragma once

#include "ProcessStages.h"

namespace msmonitor::process {

// One rank-local pipeline per instance. A daemon may run multiple instances concurrently,
// each with distinct analyser and exporter objects.
// The caller owns all stages and joins Run before destroying this object. No cross-rank barrier.
class Processor {
    // Architecture (all state and workers belong to Options.rank):
    //
    // Every 1 s: snapshot JSONL sizes -> read all visible appends in bounded chunks
    //                                      |
    //                                StepAnalyser
    //                      ProcessLine -> EndBatch -> Finish
    //                                      |
    //                           immutable StepDataOutput
    //                             +---------+--------+
    //                       metric queue          trace queue
    //                            |                    |
    //                      MetricExporter        TraceExporter
    //                      (own worker)          (own worker)
    //
    // Rank-local backpressure never waits for another rank's queue.

  public:
    Processor(Options options, StepAnalyser &analyser, MetricExporter &metricExporter, TraceExporter &traceExporter);
    ~Processor();
    Processor(const Processor &) = delete;
    Processor &operator=(const Processor &) = delete;

    // Blocking and single-use. Poll for new data until RequestStop().
    void Run();

    // Thread-safe stop request. Normal shutdown drains published steps; stage calls must return in finite time.
    void RequestStop();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace msmonitor::process
