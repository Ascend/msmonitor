// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#include "ProcessRunner.h"

#include <glog/logging.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>

#include "ProcessRunnerDetail.h"
#include "Processor.h"
#include "StepDataAnalyser.h"
#include "metric/StepMetricReporter.h"
#include "trace/StepTraceExporter.h"

namespace msmonitor::process_runner {
namespace {

using detail::ParseRankFileName;
using process::Options;
using process::Processor;
using RankFile = detail::RankFile;

std::optional<process::StepBoundary> DecodeDeviceStep(const process::StepEvent &event) {
    const auto &data = event.data;
    const auto name = data.value("name", "");
    constexpr std::string_view prefix{"step "};
    if (data.value("sourceKind", "") != "Device" || data.value("domain", "") != "default" ||
        name.rfind(prefix, 0) != 0) {
        return {};
    }
    std::uint64_t id{};
    const auto end = name.data() + name.size();
    const auto parsed = std::from_chars(name.data() + prefix.size(), end, id);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        throw std::invalid_argument("invalid device step marker");
    }
    return process::StepBoundary{id, event.startNs, event.endNs};
}

class DisabledMetrics final : public process::MetricExporter {
    void ExportStep(const process::StepDataOutput &) override {}
};

class DisabledTrace final : public process::TraceExporter {
    void WriteStep(process::StepDataPtr) override {}
};

// 目录遍历明显变慢时放宽扫描间隔，避免持续压满 NFS；本地盘上不会生效。
constexpr std::chrono::milliseconds MAX_SCAN_INTERVAL{60000};

// 运行中目录消失时保留 worker 的宽限期，覆盖 NFS 卸载/重挂等瞬时抖动。
// 超过宽限期仍未恢复才按目录为空回收 worker。
constexpr std::chrono::seconds MISSING_INPUT_DIR_GRACE{60};

// 每个 rank 一条流水线一个线程：任务长驻且数量固定，进程内不再二次池化。
class RankWorker {
  public:
    RankWorker(const RunnerConfig &config, const RankFile &rankFile, const std::string &runId)
        : rank_(rankFile.rank), processId_(rankFile.processId) {
        LOG(INFO) << "rank worker init: rank=" << rank_ << " pid=" << processId_
                  << " input_dir=" << config.inputDir.string() << " job_id=" << config.jobId
                  << " trace=" << config.enableTrace << " metric=" << config.enableMetric
                  << " server=" << config.serverHost << ":" << config.serverPort;

        Options options;
        options.inputDir = config.inputDir;
        options.rank = rank_;
        options.processId = processId_;
        options.pollInterval = config.pollInterval;
        options.queueCapacity = config.queueCapacity;
        options.recursiveInput = false;
        analyser_ = std::make_unique<process::StepDataAnalyser>(options, DecodeDeviceStep);
        if (config.enableMetric) {
            metricExporter_ = std::make_unique<process::StepMetricReporter>(
                config.jobId, runId, config.remoteWriteUrl, config.token, config.caFile);
        } else {
            metricExporter_ = std::make_unique<DisabledMetrics>();
        }
        if (config.enableTrace) {
            process::TraceOptions traceOptions;
            traceOptions.outputDir = config.inputDir / "trace" / runId / std::to_string(processId_);
            traceOptions.keepFiles = static_cast<std::size_t>(config.traceKeepFiles);
            traceOptions.stepsPerFile = std::max(1, config.traceStepsPerFile);
            traceExporter_ = std::make_unique<process::StepTraceExporter>(std::move(traceOptions));
        } else {
            traceExporter_ = std::make_unique<DisabledTrace>();
        }
        processor_ = std::make_unique<Processor>(std::move(options), *analyser_, *metricExporter_, *traceExporter_);
    }

    ~RankWorker() { Stop(); }

    std::uint64_t ProcessId() const { return processId_; }

    void Start() {
        thread_ = std::thread([this]() {
            try {
                processor_->Run();
            } catch (const std::exception &e) {
                LOG(ERROR) << "rank=" << rank_ << " processor exited: " << e.what();
            } catch (...) {
                LOG(ERROR) << "rank=" << rank_ << " processor exited with unknown error";
            }
        });
    }

    void Stop() {
        if (!thread_.joinable()) {
            return;
        }
        processor_->RequestStop();
        thread_.join();
    }

  private:
    std::int64_t rank_;
    std::uint64_t processId_;
    // stage 必须先于 processor_ 声明：Processor 只持有它们的引用。
    std::unique_ptr<process::StepDataAnalyser> analyser_;
    std::unique_ptr<process::MetricExporter> metricExporter_;
    std::unique_ptr<process::TraceExporter> traceExporter_;
    std::unique_ptr<Processor> processor_;
    std::thread thread_;
};

class RankSupervisor {
  public:
    explicit RankSupervisor(RunnerConfig config)
        : config_(std::move(config)),
          runId_(std::to_string(std::chrono::system_clock::now().time_since_epoch().count())) {}

    ~RankSupervisor() { Stop(); }

    void Start() {
        supervisor_ = std::thread([this]() { Run(); });
    }

    void Stop() {
        if (stopped_.exchange(true)) {
            return;
        }
        // 唤醒等待中的扫描线程，避免最长等一个扫描周期才能退出
        waitCv_.notify_all();
        if (supervisor_.joinable()) {
            supervisor_.join();
        }
        for (auto &entry : workers_) {
            entry.second->Stop();
        }
        workers_.clear();
    }

  private:
    void Run() {
        auto interval = config_.scanInterval;
        while (!stopped_.load()) {
            const auto begin = std::chrono::steady_clock::now();
            try {
                Reconcile();
            } catch (const std::exception &e) {
                // 单个 rank 建流水线失败不能把 supervisor 线程带崩（否则 std::terminate）
                LOG(ERROR) << "reconcile failed: " << e.what();
            } catch (...) {
                LOG(ERROR) << "reconcile failed with unknown error";
            }
            const auto cost =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin);
            if (cost > config_.scanInterval / 2) {
                LOG(WARNING) << "scan slow: " << cost.count() << "ms, ranks=" << workers_.size()
                             << ", input_dir=" << config_.inputDir.string();
            }
            VLOG(1) << "scan cost=" << cost.count() << "ms, ranks=" << workers_.size();

            // 扫描耗时增长时按十倍耗时放宽间隔，把遍历占用压在 10% 左右，上限见 MAX_SCAN_INTERVAL。
            interval = std::clamp(std::max(config_.scanInterval, cost * 10), config_.scanInterval, MAX_SCAN_INTERVAL);
            std::unique_lock<std::mutex> lock(waitMutex_);
            waitCv_.wait_for(lock, interval, [this] { return stopped_.load(); });
        }
    }

    // 目录可能尚不存在：开跑前只提示一次并继续等待训练侧落盘。
    // 返回 false 表示本轮遍历不完整，调用方应跳过增删，避免误判 rank 消失。
    bool Scan(std::map<std::int64_t, RankFile> &latestFiles) {
        latestFiles.clear();
        std::error_code ec;
        const bool dirExists = std::filesystem::exists(config_.inputDir, ec);
        if (ec) {
            // 权限、NFS 抖动等错误不能当成"目录为空"，否则会回收全部 worker
            LOG(WARNING) << "scan failed: " << ec.message();
            return false;
        }
        if (!dirExists) {
            if (!warnedMissingInputDir_) {
                LOG(WARNING) << "input_dir not found: " << config_.inputDir.string();
                warnedMissingInputDir_ = true;
            }
            // 开跑前目录还没建好：此时没有 worker 可回收，保持空结果
            if (!inputDirSeen_) {
                return true;
            }
            // 运行中目录消失多为 NFS 抖动/卸载：宽限期内按"遍历不完整"处理，保留 worker，
            // 避免恢复后重建流水线从 offset 0 重读历史行造成指标重复。
            const auto now = std::chrono::steady_clock::now();
            if (!inputDirMissing_) {
                inputDirMissing_ = true;
                inputDirMissingSince_ = now;
                LOG(WARNING) << "input_dir disappeared, keep workers for up to " << MISSING_INPUT_DIR_GRACE.count()
                             << "s";
            }
            if (now - inputDirMissingSince_ < MISSING_INPUT_DIR_GRACE) {
                return false;
            }
            // 超过宽限期视为产物目录已被清理：回收一次 worker，回到等待目录出现的状态
            LOG(WARNING) << "input_dir missing beyond grace period, release workers";
            inputDirMissing_ = false;
            inputDirSeen_ = false;
            return true;
        }
        if (warnedMissingInputDir_) {
            LOG(INFO) << "input_dir found again: " << config_.inputDir.string();
        }
        inputDirMissing_ = false;
        inputDirSeen_ = true;
        warnedMissingInputDir_ = false;

        // 落盘文件平铺在 input_dir 下，不递归。遍历只读目录项并解析文件名，
        // 不访问元数据；历史文件增多只增加字符串解析开销，不增加系统调用。
        std::filesystem::directory_iterator it(config_.inputDir, ec);
        const std::filesystem::directory_iterator end;
        if (ec) {
            LOG(WARNING) << "scan failed: " << ec.message();
            return false;
        }
        for (; it != end; it.increment(ec)) {
            if (ec) {
                LOG(WARNING) << "scan incomplete: " << ec.message();
                return false;
            }
            RankFile rankFile;
            if (!ParseRankFileName(it->path(), rankFile)) {
                continue;
            }
            auto found = latestFiles.find(rankFile.rank);
            if (found == latestFiles.end() || rankFile.timestamp > found->second.timestamp) {
                latestFiles[rankFile.rank] = rankFile;
            }
        }

        // 每个 rank 只对胜出的文件做一次类型校验，其余条目不再访问文件系统。
        for (auto it = latestFiles.begin(); it != latestFiles.end();) {
            std::error_code fileEc;
            if (!std::filesystem::is_regular_file(it->second.path, fileEc)) {
                VLOG(1) << "skip non-regular file: " << it->second.path.string();
                it = latestFiles.erase(it);
            } else {
                ++it;
            }
        }
        return true;
    }

    // 新 rank 建 worker，pid 变化重建，文件消失回收。
    void Reconcile() {
        std::map<std::int64_t, RankFile> latestFiles;
        if (!Scan(latestFiles)) {
            // 遍历不完整时保留现有 worker，等下一轮扫描结果
            return;
        }
        for (const auto &entry : latestFiles) {
            const auto found = workers_.find(entry.first);
            if (found != workers_.end() && found->second->ProcessId() == entry.second.processId) {
                // A restart without a checkpoint would replay exports and repeat deterministic failures.
                continue;
            }
            if (found != workers_.end()) {
                found->second->Stop();
                workers_.erase(found);
            }
            auto worker = std::make_unique<RankWorker>(config_, entry.second, runId_);
            worker->Start();
            workers_[entry.first] = std::move(worker);
        }

        for (auto it = workers_.begin(); it != workers_.end();) {
            if (latestFiles.find(it->first) == latestFiles.end()) {
                LOG(INFO) << "rank=" << it->first << " worker released, input file no longer present";
                it->second->Stop();
                it = workers_.erase(it);
            } else {
                ++it;
            }
        }
    }

    RunnerConfig config_;
    std::string runId_;
    std::atomic<bool> stopped_{false};
    bool warnedMissingInputDir_{false};
    // 目录一旦出现过，运行中消失先按抖动处理，宽限期后才回收 worker
    bool inputDirSeen_{false};
    bool inputDirMissing_{false};
    std::chrono::steady_clock::time_point inputDirMissingSince_{};
    // 等待扫描间隔用条件变量，Stop 时立即唤醒，不轮询空转
    std::mutex waitMutex_;
    std::condition_variable waitCv_;
    std::thread supervisor_;
    std::map<std::int64_t, std::unique_ptr<RankWorker>> workers_;
};

std::mutex g_runnerMutex;
std::unique_ptr<RankSupervisor> g_supervisor;
} // namespace

bool Start(const RunnerConfig &config) {
    std::lock_guard<std::mutex> lock(g_runnerMutex);
    if (g_supervisor) {
        LOG(WARNING) << "process runner already started";
        return false;
    }
    if (config.traceStepsPerFile < 0 || config.traceKeepFiles < 0) {
        throw std::invalid_argument("trace step count and retention must be non-negative");
    }
    LOG(INFO) << "process runner start: input_dir=" << config.inputDir.string()
              << " scan_interval_ms=" << config.scanInterval.count()
              << " poll_interval_ms=" << config.pollInterval.count() << " queue_capacity=" << config.queueCapacity
              << " trace_steps_per_file=" << config.traceStepsPerFile << " trace_keep_files=" << config.traceKeepFiles;
    auto resolved = config;
    if (resolved.enableMetric) {
        if (!resolved.tokenFile.empty()) {
            std::ifstream tokenFile(resolved.tokenFile);
            if (!(tokenFile >> resolved.token) || resolved.token.empty()) {
                throw std::invalid_argument("could not read Remote Write token file");
            }
        }
        // Validate transport configuration before starting any rank workers.
        process::PrometheusRemoteWrite transport(resolved.remoteWriteUrl, resolved.token, resolved.caFile);
    }
    g_supervisor = std::make_unique<RankSupervisor>(std::move(resolved));
    g_supervisor->Start();
    return true;
}

void Stop() {
    std::unique_ptr<RankSupervisor> supervisor;
    {
        std::lock_guard<std::mutex> lock(g_runnerMutex);
        supervisor = std::move(g_supervisor);
    }
    if (supervisor) {
        supervisor->Stop();
        LOG(INFO) << "process runner stopped";
    }
}
} // namespace msmonitor::process_runner
