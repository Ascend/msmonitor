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

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "dynolog/src/ServiceHandler.h"
#include "dynolog/src/gpumon/DcgmGroupInfo.h"
#include "dynolog/src/rpc/SimpleJsonServer.h"
#include "dynolog/src/rpc/SimpleJsonServerInl.h"
#include "dynolog/src/tracing/IPCMonitor.h"

DECLARE_int32(port);
DECLARE_bool(use_JSON);
#ifdef USE_PROMETHEUS
DECLARE_bool(use_prometheus);
#endif
DECLARE_bool(use_fbrelay);
DECLARE_bool(use_ODS);
DECLARE_bool(use_scuba);
DECLARE_int32(kernel_monitor_reporting_interval_s);
DECLARE_int32(perf_monitor_reporting_interval_s);
DECLARE_int32(dcgm_reporting_interval_s);
DECLARE_bool(enable_ipc_monitor);
DECLARE_bool(enable_gpu_monitor);
DECLARE_bool(enable_perf_monitor);

namespace dynolog
{

class ThreadManager
{
   public:
    ThreadManager() : still_alive_(true) {}
    ~ThreadManager() { stop(); }

    void run(int argc, char** argv);

   private:
    void start_threads();
    void stop();
    void gpu_monitor_loop();
    void kernel_monitor_loop();
    void perf_monitor_loop();
    void wait_until(const std::chrono::steady_clock::time_point& wakeup);

   private:
    std::atomic<bool> still_alive_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::shared_ptr<gpumon::DcgmGroupInfo> dcgm_;
    std::shared_ptr<tracing::IPCMonitor> ipcmon_;
    std::unique_ptr<std::thread> ipcmon_thread_;
    std::unique_ptr<std::thread> data_ipcmon_thread_;
    std::unique_ptr<std::thread> gpumon_thread_;
    std::unique_ptr<std::thread> pm_thread_;
    std::unique_ptr<std::thread> km_thread_;
    std::unique_ptr<SimpleJsonServer<ServiceHandler>> server_;
};
}  // namespace dynolog
