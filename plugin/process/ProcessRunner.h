// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>

#include "ProcessConstants.h"

namespace msmonitor::process_runner
{

// msmonitor start 透传的参数：由 CLI 的 Python 侧原样交给数据处理组件。
struct RunnerConfig
{
    // 扫描 inputDir 发现 rank 的间隔
    static constexpr std::chrono::milliseconds ScanInterval{2000};

    std::filesystem::path inputDir;
    std::string jobId;
    // trace 产物切分：每个 trace 文件包含的 step 数、保留的 trace 文件数。
    int traceStepsPerFile{5};
    int traceKeepFiles{100};
    std::string serverHost;
    int serverPort{8443};
    std::string remoteWriteUrl;
    std::string tokenFile;
    // 明文 token，与 tokenFile 二选一，由 CLI 通过环境变量传入。
    std::string token;
    std::string caFile;
    bool enableTrace{true};
    bool enableMetric{true};
    std::chrono::milliseconds pollInterval{process::ProcessConstants::PollInterval};
    std::size_t queueCapacity{process::ProcessConstants::QueueCapacity};
    std::chrono::milliseconds scanInterval{ScanInterval};
};

// 非阻塞：起 supervisor 线程后立即返回；重复调用返回 false。
bool Start(const RunnerConfig &config);

// 置停止位，等待 supervisor 与各 rank 线程退出；可重复调用。
void Stop();

}  // namespace msmonitor::process_runner
