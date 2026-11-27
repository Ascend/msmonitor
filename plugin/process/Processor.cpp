// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#include "Processor.h"

#include <glog/logging.h>

#include <atomic>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include "JsonlReader.h"
#include "StepQueue.h"

namespace msmonitor::process
{

struct Processor::Impl
{
    Impl(Options config, StepAnalyser &analyser, MetricExporter &metricExporter, TraceExporter &traceExporter)
        : options_(std::move(config)),
          reader_(options_),
          analyser_(analyser),
          metricExporter_(metricExporter),
          traceExporter_(traceExporter),
          metricQueue_(options_.queueCapacity),
          traceQueue_(options_.queueCapacity)
    {
    }

    // Start concurrent consumers, process input, then drain queues and join both workers.
    void Run()
    {
        if (used_.exchange(true))
        {
            throw std::logic_error("Processor is single-use");
        }
        LOG(INFO) << "Process started, rank: " << options_.rank << ", input: " << options_.inputDir.string();
        try
        {
            metricThread_ = StartExportWorker(
                "metric export", metricQueue_, [this](const StepDataPtr &step) { metricExporter_.ExportStep(*step); },
                [] {});
            traceThread_ = StartExportWorker(
                "trace export", traceQueue_, [this](const StepDataPtr &step) { traceExporter_.WriteStep(step); },
                [this] { traceExporter_.Flush(); });
            ReadJsonlAndPublishSteps();
        }
        catch (...)
        {
            Fail("JSONL pipeline/startup", std::current_exception());
        }
        DrainAndJoinWorkers();
        if (failure_)
        {
            std::rethrow_exception(failure_);
        }
        LOG(INFO) << "Process finished, rank: " << options_.rank;
    }

    // Change scheduling state without accessing stage objects from another thread.
    void RequestStop()
    {
        {
            // Synchronise with the wait predicate so a stop cannot miss a sleeping reader.
            std::lock_guard<std::mutex> lock(waitMutex_);
            stop_ = true;
        }
        wake_.notify_all();
    }

   private:
    // Each consumer drains its own queue and reports failures with the current step.
    template <typename ExportStep, typename FinishExport>
    std::thread StartExportWorker(const char *stage, StepQueue &queue, ExportStep exportStep, FinishExport finish)
    {
        return std::thread(
            [this, stage, &queue, exportStep, finish]
            {
                StepDataPtr step;
                try
                {
                    while ((step = queue.Pop()))
                    {
                        exportStep(step);
                        VLOG(1) << "Process step exported, rank: " << step->rank << ", step: " << step->boundary.stepId
                                << ", stage: " << stage;
                        step.reset();
                    }
                    finish();
                }
                catch (...)
                {
                    Fail(stage, std::current_exception(), step.get());
                }
            });
    }

    void ReadJsonlAndPublishSteps()
    {
        const EmitStepData publish = [this](StepDataPtr step) { EnqueueStepForExport(std::move(step)); };
        const auto analyseLine = [this](const JsonlLine &line)
        {
            try
            {
                analyser_.ProcessLine(line);
            }
            catch (const std::exception &e)
            {
                throw std::runtime_error(line.file.string() + ":" + std::to_string(line.lineNumber) + ": " + e.what());
            }
        };
        while (!stop_.load())
        {
            const auto nextPoll = std::chrono::steady_clock::now() + options_.pollInterval;
            const auto batch = reader_.Poll(analyseLine);
            analyser_.EndBatch(batch, publish);
            // Pace polls even when new data was read.
            if (std::chrono::steady_clock::now() > nextPoll)
            {
                LOG(WARNING) << "Process read/analysis batch exceeded poll interval, rank: " << options_.rank
                             << ", poll interval ms: " << options_.pollInterval.count();
            }
            std::unique_lock<std::mutex> lock(waitMutex_);
            if (wake_.wait_until(lock, nextPoll, [this] { return stop_.load(); }))
            {
                break;
            }
        }
        reader_.Finish(false, analyseLine);
        analyser_.Finish(false, publish);
    }

    void EnqueueStepForExport(StepDataPtr step)
    {
        if (!step)
        {
            throw std::invalid_argument("step analyser published a null step");
        }
        // Share immutable results. Either slow consumer applies backpressure without silently dropping steps.
        if (!metricQueue_.Push(step) || !traceQueue_.Push(step))
        {
            throw std::runtime_error("consumer queue closed");
        }
    }

    // Shared by normal shutdown and failure handling; closing a queue is idempotent.
    void CloseQueues()
    {
        metricQueue_.Close();
        traceQueue_.Close();
    }

    void DrainAndJoinWorkers()
    {
        CloseQueues();
        if (metricThread_.joinable())
        {
            metricThread_.join();
        }
        if (traceThread_.joinable())
        {
            traceThread_.join();
        }
    }

    // Keep the first error, unblock producers, then rethrow after joining. Branches have no atomic rollback.
    void Fail(const char *stage, std::exception_ptr error, const StepDataOutput *step = nullptr)
    {
        {
            std::lock_guard<std::mutex> lock(errorMutex_);
            if (failure_)
            {
                return;
            }
            failure_ = error;
        }
        CloseQueues();
        RequestStop();
        const auto stepContext = step ? ", step: " + std::to_string(step->boundary.stepId) : std::string{};
        try
        {
            std::rethrow_exception(error);
        }
        catch (const std::exception &e)
        {
            LOG(ERROR) << "Process failed, rank: " << options_.rank << ", stage: " << stage << stepContext << ": "
                       << e.what();
        }
        catch (...)
        {
            LOG(ERROR) << "Process failed, rank: " << options_.rank << ", stage: " << stage << stepContext
                       << ": unknown exception";
        }
    }

    // Own the reader and configuration; the caller owns the three processing stages.
    Options options_;
    JsonlReader reader_;
    StepAnalyser &analyser_;
    MetricExporter &metricExporter_;
    TraceExporter &traceExporter_;

    // Independent bounded queues and consumers share immutable step results.
    StepQueue metricQueue_;
    StepQueue traceQueue_;
    std::thread metricThread_;
    std::thread traceThread_;

    // Lifecycle and failure state; the first error reaches the caller after all workers exit.
    std::atomic<bool> stop_{false};
    std::atomic<bool> used_{false};
    std::mutex errorMutex_;
    std::mutex waitMutex_;
    std::condition_variable wake_;
    std::exception_ptr failure_;
};

Processor::Processor(Options options, StepAnalyser &analyser, MetricExporter &metricExporter,
                     TraceExporter &traceExporter)
{
    if (options.rank < ProcessConstants::Unranked ||
        (options.rank == ProcessConstants::Unranked && !options.processId) || analyser.Rank() != options.rank ||
        !options.queueCapacity || !options.readChunkBytes || !options.maxLineBytes ||
        options.pollInterval.count() <= 0 || !std::filesystem::is_directory(options.inputDir))
    {
        throw std::invalid_argument("invalid processor options/input directory");
    }
    impl_ = std::make_unique<Impl>(std::move(options), analyser, metricExporter, traceExporter);
}

Processor::~Processor() = default;

void Processor::Run() { impl_->Run(); }

void Processor::RequestStop() { impl_->RequestStop(); }
}  // namespace msmonitor::process
