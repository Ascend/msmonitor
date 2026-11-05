/*
 * -------------------------------------------------------------------------
 * This file is part of the MindStudio project.
 * Copyright (c) 2025 Huawei Technologies Co.,Ltd.
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

// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "dynolog/src/LibkinetoConfigManager.h"

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <glog/logging.h>

#include <array>
#include <atomic>
#include <exception>
#include <fstream>
#include <iterator>
#include <thread>
#include <type_traits>
#include <utility>

#include "dynolog/src/ipcfabric/FabricManager.h"
#include "hbt/src/common/System.h"
#ifdef __linux__
#include <sys/prctl.h>
#endif

namespace dynolog
{

namespace
{

const int VerboseLevel = 2;
constexpr int kMaxRegisterCount = 50;
constexpr size_t kMaxPendingActivityConfigs = 32;  // bounds per-process memory and backlog drain time
constexpr std::chrono::seconds kKeepAliveTimeSecs(60);
constexpr char kConfigFile[] = "/etc/libkineto.conf";

inline void setThreadName(const std::string& name)
{
#ifdef __linux__
    constexpr size_t kMaxBuff = 16;
    std::array<char, kMaxBuff> buff;
    std::size_t len = name.copy(buff.begin(), 0, kMaxBuff - 1);
    buff[len] = '\0';
    ::prctl(PR_SET_NAME, buff.begin(), 0, 0, 0);
#endif
}

}  // namespace

static std::string addTraceIdToConfigString(const std::string& trace_id, const std::string& config)
{
    const std::string kTraceIdIdentifier = "REQUEST_TRACE_ID";
    return fmt::format(
        R"(
            {}
            {}={})",
        config, kTraceIdIdentifier, trace_id);
}

static std::string generateTraceId(int32_t pid)
{
    // Hostname + PID + timestamp should be a unique trace id in the context
    // of this code's execution
    static std::atomic_uint64_t counter{0};
    std::string str_trace_id =
        fmt::format("{}:{}:{}:{}", facebook::hbt::getHostName(), pid, std::time(nullptr), counter.fetch_add(1));
    std::size_t hashed_trace_id = std::hash<std::string>{}(str_trace_id);
    return std::to_string(hashed_trace_id);
}

// Batch framing: latest config + DYNO_* keys + 0x1e + payload (legacy head kept).
constexpr char kConfigRecordSeparator = '\x1e';
constexpr char kConfigCountKey[] = "DYNO_BATCH_COUNT";
constexpr char kConfigLenKey[] = "DYNO_BATCH_LEN";
constexpr char kConfigSumKey[] = "DYNO_BATCH_SUM";

static uint64_t fnv1a64(const std::string& data)
{
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (const unsigned char byte : data)
    {
        hash ^= byte;
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

static std::string fnv1a64Hex(uint64_t value)
{
    static const char* kHexDigits = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i)
    {
        out[i] = kHexDigits[value & 0xF];
        value >>= 4;
    }
    return out;
}

static std::string trimLineHead(const std::string& line)
{
    const size_t start = line.find_first_not_of(" \t\r");
    return start == std::string::npos ? std::string() : line.substr(start);
}

static bool isReservedFramingLine(const std::string& line)
{
    const std::string head = trimLineHead(line);
    static const char* kReservedKeys[] = {kConfigCountKey, kConfigLenKey, kConfigSumKey};
    for (const char* key : kReservedKeys)
    {
        const std::string prefix = std::string(key) + "=";
        if (head.compare(0, prefix.size(), prefix) == 0)
        {
            return true;
        }
    }
    return false;
}

static bool isValidActivityConfig(const std::string& config)
{
    if (config.empty() || config.find(kConfigRecordSeparator) != std::string::npos)
    {
        return false;
    }
    size_t pos = 0;
    while (pos <= config.size())
    {
        const size_t lineEnd = config.find('\n', pos);
        const std::string line = lineEnd == std::string::npos ? config.substr(pos) : config.substr(pos, lineEnd - pos);
        if (isReservedFramingLine(line))
        {
            return false;
        }
        if (lineEnd == std::string::npos)
        {
            break;
        }
        pos = lineEnd + 1;
    }
    return true;
}

// IPC wire payload limit for dynolog -> plugin replies. The effective receiver
// is the ipc_monitor plugin (plugin/ipc_monitor/NpuIpcClient.h:
// MAX_IPC_MESSAGE_SIZE); keep this aligned with it. ipcfabric::MAX_MSG_SIZE is
// the server-side receive cap and currently shares the same value.
constexpr size_t kMaxIpcMessageBytes = 4096;
static_assert(kMaxIpcMessageBytes == ipcfabric::MAX_MSG_SIZE,
              "update together with plugin MAX_IPC_MESSAGE_SIZE when changing the IPC limit");
constexpr size_t kIpcHeaderReserve = 196;  // safety margin for IPC bookkeeping
constexpr size_t kMaxBatchFrameBytes = kMaxIpcMessageBytes - kIpcHeaderReserve;
constexpr size_t kFrameOverheadBytes = 256;
// The legacy head and the batched payload both carry the newest config.
constexpr size_t kLatestConfigCopies = 2;

static std::string buildActivityFrame(const std::vector<std::string>& configs, size_t count)
{
    std::string payload;
    for (size_t i = 0; i < count; ++i)
    {
        if (i)
        {
            payload += kConfigRecordSeparator;
        }
        payload += configs[i];
    }
    std::string frame = configs[count - 1];
    frame += "\n" + std::string(kConfigCountKey) + "=" + std::to_string(count);
    frame += "\n" + std::string(kConfigLenKey) + "=" + std::to_string(payload.size());
    frame += "\n" + std::string(kConfigSumKey) + "=" + fnv1a64Hex(fnv1a64(payload));
    frame += kConfigRecordSeparator;
    frame += payload;
    return frame;
}

// Returns as many queued configs as fit in maxBytes; empty means retry later.
static std::string serializeActivityProfilerConfigs(const std::vector<std::string>& configs, size_t maxBytes,
                                                    size_t* sentCount)
{
    *sentCount = 0;
    for (size_t count = configs.size(); count > 0; --count)
    {
        std::string frame = buildActivityFrame(configs, count);
        if (frame.size() <= maxBytes)
        {
            *sentCount = count;
            return frame;
        }
    }
    LOG(WARNING) << "IPC message budget exhausted by the event config, activity configs deferred.";
    return "";
}

LibkinetoConfigManager::LibkinetoConfigManager() { managerThread_ = std::thread(&LibkinetoConfigManager::start, this); }

LibkinetoConfigManager::~LibkinetoConfigManager()
{
    stopFlag_ = true;
    managerCondVar_.notify_one();
    if (managerThread_.joinable())
    {
        managerThread_.join();
    }
}

std::shared_ptr<LibkinetoConfigManager> LibkinetoConfigManager::getInstance()
{
    static auto instance = std::make_shared<LibkinetoConfigManager>();
    return instance;
}

void LibkinetoConfigManager::start()
{
    setThreadName("kinetoConfigMgr");
    // Periodically clean the job table and check base config changes.
    // If a libkineto instance hasn't contacted us for a while, remove it.
    LOG(INFO) << "Starting LibkinetoConfigManager runloop";
    while (true)
    {
        refreshBaseConfig();
        std::unique_lock<std::mutex> lock(mutex_);
        managerCondVar_.wait_for(lock, kKeepAliveTimeSecs);
        if (stopFlag_)
        {
            break;
        }
        runGc();
    }
}

// return "" on errors. Otherwise a config string.
static std::string readConfigFromConfigFile(const char* filename)
{
    // Read whole file into a string.
    std::ifstream file(filename);
    if (!file)
    {
        return "";
    }
    std::string conf;
    try
    {
        conf.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Error in reading libkineto config from config file: " << e.what();
    }
    return conf;
}

void LibkinetoConfigManager::refreshBaseConfig()
{
    auto cfg = readConfigFromConfigFile(kConfigFile);
    if (!cfg.empty() && cfg != baseConfig_)
    {
        std::lock_guard<std::mutex> guard(mutex_);
        baseConfig_ = cfg;
    }
}

void LibkinetoConfigManager::runGc()
{
    auto t = std::chrono::system_clock::now();
    int job_count = jobs_.size();
    for (auto job_it = jobs_.begin(); job_it != jobs_.end();)
    {
        auto& procs = job_it->second;
        for (auto proc_it = procs.begin(); proc_it != procs.end();)
        {
            struct LibkinetoProcess& proc = proc_it->second;
            if ((t - proc.lastRequestTime) > kKeepAliveTimeSecs)
            {
                LOG(INFO) << fmt::format("Stopped tracking process ({}) from job {}", fmt::join(proc_it->first, ","),
                                         job_it->first);
                onProcessCleanup(proc_it->first);
                proc_it = procs.erase(proc_it);
                registerCount_--;
            }
            else
            {
                proc_it++;
            }
        }
        if (procs.empty())
        {
            LOG(INFO) << "Stopped tracking job " << job_it->first;
            jobInstancesPerGpu_.erase(job_it->first);
            job_it = jobs_.erase(job_it);
        }
        else
        {
            job_it++;
        }
    }
    if (job_count != jobs_.size())
    {
        LOG(INFO) << "Tracked jobs: " << jobs_.size();
    }
}

int32_t LibkinetoConfigManager::registerLibkinetoContext(const std::string& jobId, int32_t pid, int32_t gpu)
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (registerCount_ >= kMaxRegisterCount)
    {
        LOG(ERROR) << "Max register count reached, can not register more libkineto contexts.";
        return -1;
    }
    auto& instances = jobInstancesPerGpu_[jobId][gpu];
    instances.insert(pid);
    LOG(INFO) << fmt::format("Registered process ({}) for job {}.", pid, jobId);
    registerCount_++;
    return instances.size();
}

// Called by libkineto instances periodically.
// In addition to returning a configuration string if one is found,
// register the jobId and set of pids with the config manager.
// This is how we keep track of running instances of libkineto.
// LibkinetoConfigManager::run() periodically scans the table
// for processes no longer calling this function and removes them.
OnDemandBatch LibkinetoConfigManager::obtainOnDemandBatch(const std::string& jobId, const std::vector<int32_t>& pids,
                                                          int32_t configType)
{
    VLOG(VerboseLevel) << fmt::format("obtainOnDemandBatch({}, ({}), {})", jobId, fmt::join(pids, ","), configType);
    OnDemandBatch batch;
    std::set<int32_t> pids_set(pids.begin(), pids.end());
    std::lock_guard<std::mutex> guard(mutex_);

    auto _emplace_result = jobs_[jobId].emplace(pids_set, LibkinetoProcess{});
    const auto& it = _emplace_result.first;
    bool newProcess = _emplace_result.second;
    struct LibkinetoProcess& process = it->second;

    if (newProcess)
    {
        // First time - intialize!
        // 'pids' is an ordered ancestor list starting with the
        // child (leaf) process, i.e. the one making this request.
        // Keep a copy of this pid so that clients can know which
        // pids are being profiled.
        process.pid = pids[0];  // Remember child (leaf) process
        LOG(INFO) << fmt::format("Registered process ({}) for job {}.", fmt::join(pids, ", "), jobId);

        onRegisterProcess(pids_set);
    }

    if ((configType & int(LibkinetoConfigType::EVENTS)) && !process.eventProfilerConfig.empty())
    {
        batch.payload += process.eventProfilerConfig + "\n";
        batch.eventIncluded = true;
    }

    if ((configType & int(LibkinetoConfigType::ACTIVITIES)) && !process.activityProfilerConfigs.empty())
    {
        // Event section first; the activity frame gets the remaining budget.
        const size_t usedBytes = batch.payload.size();
        const size_t remainingBytes = usedBytes < kMaxBatchFrameBytes ? kMaxBatchFrameBytes - usedBytes : 0;
        size_t sentCount = 0;
        const std::string frame =
            serializeActivityProfilerConfigs(process.activityProfilerConfigs, remainingBytes, &sentCount);
        if (sentCount > 0)
        {
            batch.payload += frame;
            batch.activityCount = sentCount;
            // Same base for every peek until an ack advances the cursor, so
            // repeated polls stay retryable and duplicate acks are harmless.
            batch.seq = process.ackedActivityCount;
        }
    }
    // Track last request time so we know which libkineto instances
    // are currently active.
    process.lastRequestTime = std::chrono::system_clock::now();
    return batch;
}

bool LibkinetoConfigManager::ackOnDemandBatch(const std::string& jobId, const std::vector<int32_t>& pids,
                                              int32_t configType, const OnDemandBatch& batch)
{
    std::lock_guard<std::mutex> guard(mutex_);
    auto job_it = jobs_.find(jobId);
    if (job_it == jobs_.end())
    {
        return false;
    }
    auto proc_it = job_it->second.find(std::set<int32_t>(pids.begin(), pids.end()));
    if (proc_it == job_it->second.end())
    {
        return false;
    }
    LibkinetoProcess& process = proc_it->second;
    if (batch.activityCount > 0 &&
        (batch.seq != process.ackedActivityCount || batch.activityCount > process.activityProfilerConfigs.size()))
    {
        VLOG(VerboseLevel) << "Ignore stale on-demand config ack for PID: " << process.pid;
        return false;
    }
    if ((configType & int(LibkinetoConfigType::EVENTS)) && batch.eventIncluded)
    {
        process.eventProfilerConfig.clear();
    }
    if (batch.activityCount > 0)
    {
        process.activityProfilerConfigs.erase(process.activityProfilerConfigs.begin(),
                                              process.activityProfilerConfigs.begin() + batch.activityCount);
        process.ackedActivityCount += batch.activityCount;
    }
    return true;
}

void LibkinetoConfigManager::setOnDemandConfigForProcess(GpuProfilerResult& res, LibkinetoProcess& process,
                                                         const std::string& config,
                                                         int32_t configType /* LibkinetoConfigType */, int32_t limit)
{
    res.processesMatched.push_back(process.pid);

    const bool eventsRequested = (configType & int(LibkinetoConfigType::EVENTS)) != 0;
    if (res.eventProfilersTriggered.size() < limit && eventsRequested)
    {
        if (config.size() + 1 > kMaxBatchFrameBytes)
        {
            res.eventProfilersBusy++;
            LOG(ERROR) << "Event config for PID " << process.pid
                       << " is too large for one IPC message, request rejected.";
        }
        else if (process.eventProfilerConfig.empty())
        {
            process.eventProfilerConfig = config;
            res.eventProfilersTriggered.push_back(process.pid);
        }
        else
        {
            res.eventProfilersBusy++;
        }
    }
    if (!(configType & int(LibkinetoConfigType::ACTIVITIES)) || res.activityProfilersTriggered.size() >= limit)
    {
        return;
    }
    if (process.activityProfilerConfigs.size() >= kMaxPendingActivityConfigs)
    {
        res.activityProfilersBusy++;
        LOG(WARNING) << "Activity profiler queue full for PID: " << process.pid << ", request rejected as busy.";
        return;
    }

    const std::string trace_id = generateTraceId(process.pid);
    const std::string updatedConfig = addTraceIdToConfigString(trace_id, config);
    // The newest config is duplicated in the legacy head and the batched payload.
    size_t estimatedBytes = kLatestConfigCopies * updatedConfig.size() + kFrameOverheadBytes;
    if (!process.eventProfilerConfig.empty())
    {
        // Any pending event config is prepended to the same poll reply.
        estimatedBytes += process.eventProfilerConfig.size() + 1;
    }
    if (estimatedBytes > kMaxBatchFrameBytes)
    {
        res.activityProfilersBusy++;
        LOG(ERROR) << "Activity config for PID " << process.pid
                   << " is too large for one IPC message, request rejected.";
        return;
    }

    preCheckOnDemandConfig(process);
    process.activityProfilerConfigs.push_back(updatedConfig);
    res.activityProfilersTriggered.push_back(process.pid);
    res.traceIds.push_back(trace_id);
    LOG(INFO) << " PID: " << process.pid << ", Trace Id: " << trace_id;
}

// Called by clients to control one or more libkineto instances.
// The config is any legal libkineto on-demand config (see wiki).
// Set config type to indicate whether this request is for
// event profiling, activity profiling or both.
// The limit argument is used when the job uses multiple processes or
// the pid is a parent pid of multiple processes with libkineto.
// For example, when specifying a pid with 8 child processes,
// the limit argument can be used to profile 2 of those.
GpuProfilerResult LibkinetoConfigManager::setOnDemandConfig(const std::string& jobId, const std::set<int32_t>& pids,
                                                            const std::string& config,
                                                            int32_t configType /* LibkinetoConfigType */, int32_t limit)
{
    LOG(INFO) << fmt::format(

        "Initiating on-demand GPU profiling for job ID {}, pids [{}]", jobId, fmt::join(pids, ","));

    GpuProfilerResult res;
    res.activityProfilersBusy = 0;
    res.eventProfilersBusy = 0;

    if ((configType & int(LibkinetoConfigType::ACTIVITIES)) && !isValidActivityConfig(config))
    {
        LOG(ERROR) << "Reject activity config for job " << jobId
                   << ": it must be non-empty, must not contain the record separator (0x1e) and must not use "
                      "reserved DYNO_* framing keys.";
        return res;
    }

    size_t nPids = pids.size();
    // For backwards compatibility with older versions of the dyno CLI,
    // there are two conditions under which all processes should be traced:
    // 1. target PIDs are empty
    // 2. target PIDs contain a single PID, 0.
    // As older versions of the CLI are phased out, 2) will no longer need to be
    // accounted for.
    bool traceAllPids = nPids == 0 || (nPids == 1 && *pids.begin() == 0);
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (auto it = jobs_.find(jobId); it != jobs_.end())
        {
            auto& processes = it->second;
            for (auto& pair : processes)
            {
                for (const auto& pid : pair.first)
                {
                    // Trace the process if we find a match or target pids is empty.
                    if (traceAllPids || pids.find(pid) != pids.end())
                    {
                        auto& process = pair.second;
                        setOnDemandConfigForProcess(res, process, config, configType, limit);
                        // the user could provide multiple pids that belong to the same the
                        // LibkientoProcess object, so we break after the first match for
                        // the LibkinetoProcess.
                        break;
                    }
                }
            }
            if (res.activityProfilersTriggered.size() > 0)
            {
                onSetOnDemandConfig(pids);
            }
        }
    }

    LOG(INFO) << "On-demand request: " << res.processesMatched.size() << " matching processes";
    if (configType & int(LibkinetoConfigType::EVENTS))
    {
        LOG(INFO) << "Installed event profiler config for " << res.eventProfilersTriggered.size() << " process(es) "
                  << "(" << res.eventProfilersBusy << " busy)";
    }
    if (configType & int(LibkinetoConfigType::ACTIVITIES))
    {
        LOG(INFO) << "Installed activity profiler config for " << res.activityProfilersTriggered.size()
                  << " process(es) " << "(" << res.activityProfilersBusy << " busy)";
    }
    return res;
}

int LibkinetoConfigManager::processCount(const std::string& jobId) const
{
    int count = 0;
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = jobs_.find(jobId);
    if (it != jobs_.end())
    {
        count = it->second.size();
    }
    LOG(INFO) << "Process count for job ID " << jobId << ": " << count;
    return count;
}

void LibkinetoConfigManager::updateNpuStatus(const NpuStatus& status, const std::string& msgType)
{
    // jobId, pid为预留参数，目前无用
    std::lock_guard<std::mutex> guard(mutex_);
    if (msgType == kLibkinetoTraceStatus)
    {
        npuTraceStatus_ = status;
    }
    else if (msgType == kLibkinetoMonitorStatus)
    {
        npuMonitorStatus_ = status;
    }
}

NpuStatus LibkinetoConfigManager::getNpuTraceStatus() const
{
    std::lock_guard<std::mutex> guard(mutex_);
    return npuTraceStatus_;
}

NpuStatus LibkinetoConfigManager::getNpuMonitorStatus() const
{
    std::lock_guard<std::mutex> guard(mutex_);
    return npuMonitorStatus_;
}

}  // namespace dynolog
