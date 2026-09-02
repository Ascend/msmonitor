/*
 * Copyright (C) 2026-2026. Huawei Technologies Co., Ltd. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef MSMONITOR_DCMI_METRICS_H
#define MSMONITOR_DCMI_METRICS_H

#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "dcmi/DcmiApiLoader.h"
#include "dcmi/DcmiMetricBase.h"

namespace dynolog_npu
{
namespace ipc_monitor
{
namespace dcmi
{

// DCMI 结构体布局（dlopen 方式不包含真实头文件，自行声明；v1/v2 布局一致，
// 依据 950 的 dcmi_interface_api.h 与 A2/A3/训练卡文档核对）。
namespace detail
{

// dcmi_freq_type 数值码位：2=控制CPU(AICPU) 6=片上内存(HBM) 7=AI Core当前 9=AI Core额定
constexpr int FREQ_TYPE_AICPU = 2;
constexpr int FREQ_TYPE_HBM = 6;
constexpr int FREQ_TYPE_AICORE_CURRENT = 7;
constexpr int FREQ_TYPE_AICORE_RATED = 9;

// dcmi_get_device_utilization_rate 的 type 码位：2=AI Core 3=AI CPU 12=vector 13=NPU整体 14=AI Cube
constexpr int UTIL_TYPE_AICORE = 2;
constexpr int UTIL_TYPE_AICPU = 3;
constexpr int UTIL_TYPE_VECTOR = 12;
constexpr int UTIL_TYPE_NPU = 13;
constexpr int UTIL_TYPE_AICUBE = 14;

struct DcmiAicoreInfo
{
    unsigned int freq;
    unsigned int cur_freq;
};

struct DcmiAicpuInfo
{
    unsigned int max_freq;
    unsigned int cur_freq;
    unsigned int aicpu_num;
    unsigned int util_rate[64];  // 官方 MAX_CORE_NUM=16，64 为安全上界
};

struct DcmiHbmInfo
{
    unsigned long long memory_size;   // MB
    unsigned int freq;                // MHz
    unsigned long long memory_usage;  // MB
    int temp;                         // ℃
    unsigned int bandwith_util_rate;  // %
};

struct DcmiMultiUtilizationInfo
{
    unsigned int aic_util;
    unsigned int aiv_util;
    unsigned int aicore_util;
    unsigned int npu_util;
    unsigned int reserved[8];
};

inline void SetInactive(monitor::DcmiMetricKind kind, const std::string &reason,
                        std::unordered_map<monitor::DcmiMetricKind, monitor::DcmiKindStatus> &status)
{
    status[kind] = monitor::DcmiKindStatus{false, reason};
}

inline void SetActive(monitor::DcmiMetricKind kind,
                      std::unordered_map<monitor::DcmiMetricKind, monitor::DcmiKindStatus> &status)
{
    status[kind] = monitor::DcmiKindStatus{true, ""};
}
}  // namespace detail

// DEVICE 层：功耗（raw 单位 0.1W，×0.1 → W）/ 温度（℃）
class PowerMetric : public DcmiMetricBase
{
   public:
    std::string GroupName() const override { return "Power"; }
    std::vector<monitor::DcmiMetricKind> Provides() const override { return {monitor::DcmiMetricKind::Power}; }
    bool Init(const DcmiApiLoader &api, const std::set<monitor::DcmiMetricKind> &enabled, const DcmiProbeDevice &probe,
              std::unordered_map<monitor::DcmiMetricKind, monitor::DcmiKindStatus> &status) override
    {
        api_ = &api;
        enabled_ = enabled.count(monitor::DcmiMetricKind::Power) > 0;
        if (!api.HasPower())
        {
            detail::SetInactive(monitor::DcmiMetricKind::Power, "symbol missing", status);
            return false;
        }
        int raw = 0;
        if (probe.cardId >= 0 && api.GetPower(probe.cardId, probe.devId, probe.flatDevId, &raw) != 0)
        {
            detail::SetInactive(monitor::DcmiMetricKind::Power, "probe call failed", status);
            return false;
        }
        detail::SetActive(monitor::DcmiMetricKind::Power, status);
        return enabled_;
    }
    int Collect(int cardId, int devId, uint32_t flatDevId, uint64_t tsNs, DcmiSampleSink &sink) override
    {
        int raw = 0;
        int ret = api_->GetPower(cardId, devId, flatDevId, &raw);
        if (ret != 0)
        {
            return ret;
        }
        sink.push_back(
            monitor::DcmiSample{monitor::DcmiMetricKind::Power, tsNs, static_cast<int32_t>(flatDevId), raw * 0.1});
        return 0;
    }

   private:
    const DcmiApiLoader *api_ = nullptr;
    bool enabled_ = false;
};

class TempMetric : public DcmiMetricBase
{
   public:
    std::string GroupName() const override { return "Temp"; }
    std::vector<monitor::DcmiMetricKind> Provides() const override { return {monitor::DcmiMetricKind::Temp}; }
    bool Init(const DcmiApiLoader &api, const std::set<monitor::DcmiMetricKind> &enabled, const DcmiProbeDevice &probe,
              std::unordered_map<monitor::DcmiMetricKind, monitor::DcmiKindStatus> &status) override
    {
        api_ = &api;
        enabled_ = enabled.count(monitor::DcmiMetricKind::Temp) > 0;
        if (!api.HasTemperature())
        {
            detail::SetInactive(monitor::DcmiMetricKind::Temp, "symbol missing", status);
            return false;
        }
        int raw = 0;
        if (probe.cardId >= 0 && api.GetTemperature(probe.cardId, probe.devId, probe.flatDevId, &raw) != 0)
        {
            detail::SetInactive(monitor::DcmiMetricKind::Temp, "probe call failed", status);
            return false;
        }
        detail::SetActive(monitor::DcmiMetricKind::Temp, status);
        return enabled_;
    }
    int Collect(int cardId, int devId, uint32_t flatDevId, uint64_t tsNs, DcmiSampleSink &sink) override
    {
        int raw = 0;
        int ret = api_->GetTemperature(cardId, devId, flatDevId, &raw);
        if (ret != 0)
        {
            return ret;
        }
        sink.push_back(monitor::DcmiSample{monitor::DcmiMetricKind::Temp, tsNs, static_cast<int32_t>(flatDevId),
                                           static_cast<double>(raw)});
        return 0;
    }

   private:
    const DcmiApiLoader *api_ = nullptr;
    bool enabled_ = false;
};

// AICORE 层：当前/额定频率。v1 符号缺失时降级 frequency type 7/9（v2 恒有 aicore_info）
class AicoreInfoMetric : public DcmiMetricBase
{
   public:
    std::string GroupName() const override { return "AicoreInfo"; }
    std::vector<monitor::DcmiMetricKind> Provides() const override
    {
        return {monitor::DcmiMetricKind::AICoreFreq, monitor::DcmiMetricKind::AICoreRatedFreq};
    }
    bool Init(const DcmiApiLoader &api, const std::set<monitor::DcmiMetricKind> &enabled, const DcmiProbeDevice &probe,
              std::unordered_map<monitor::DcmiMetricKind, monitor::DcmiKindStatus> &status) override
    {
        api_ = &api;
        needCur_ = enabled.count(monitor::DcmiMetricKind::AICoreFreq) > 0;
        needRated_ = enabled.count(monitor::DcmiMetricKind::AICoreRatedFreq) > 0;
        if (!needCur_ && !needRated_)
        {
            return false;
        }
        if (api.HasAicoreInfo())
        {
            mode_ = Mode::kInfo;
        }
        else if (api.HasFrequency())
        {
            mode_ = Mode::kFreq;  // v1 aicore_info 缺失时的兜底
        }
        else
        {
            detail::SetInactive(monitor::DcmiMetricKind::AICoreFreq, "symbols missing", status);
            detail::SetInactive(monitor::DcmiMetricKind::AICoreRatedFreq, "symbols missing", status);
            return false;
        }
        detail::DcmiAicoreInfo info{};
        unsigned int raw = 0;
        bool ok = (mode_ == Mode::kInfo) ? api.GetAicoreInfo(probe.cardId, probe.devId, probe.flatDevId, &info) == 0
                                         : api.GetFrequency(probe.cardId, probe.devId, probe.flatDevId,
                                                            detail::FREQ_TYPE_AICORE_CURRENT, &raw) == 0;
        if (probe.cardId >= 0 && !ok)
        {
            detail::SetInactive(monitor::DcmiMetricKind::AICoreFreq, "probe call failed", status);
            detail::SetInactive(monitor::DcmiMetricKind::AICoreRatedFreq, "probe call failed", status);
            return false;
        }
        if (needCur_)
        {
            detail::SetActive(monitor::DcmiMetricKind::AICoreFreq, status);
        }
        if (needRated_)
        {
            detail::SetActive(monitor::DcmiMetricKind::AICoreRatedFreq, status);
        }
        return true;
    }
    int Collect(int cardId, int devId, uint32_t flatDevId, uint64_t tsNs, DcmiSampleSink &sink) override
    {
        detail::DcmiAicoreInfo info{};
        unsigned int cur = 0;
        unsigned int rated = 0;
        int ret = 0;
        if (mode_ == Mode::kInfo)
        {
            ret = api_->GetAicoreInfo(cardId, devId, flatDevId, &info);
            if (ret != 0)
            {
                return ret;
            }
        }
        else
        {
            if (needCur_)
            {
                ret = api_->GetFrequency(cardId, devId, flatDevId, detail::FREQ_TYPE_AICORE_CURRENT, &cur);
                if (ret != 0)
                {
                    return ret;
                }
            }
            if (needRated_)
            {
                ret = api_->GetFrequency(cardId, devId, flatDevId, detail::FREQ_TYPE_AICORE_RATED, &rated);
                if (ret != 0)
                {
                    return ret;
                }
            }
        }
        int32_t flat = static_cast<int32_t>(flatDevId);
        if (needCur_)
        {
            sink.push_back(monitor::DcmiSample{
                monitor::DcmiMetricKind::AICoreFreq, tsNs, flat,
                mode_ == Mode::kInfo ? static_cast<double>(info.cur_freq) : static_cast<double>(cur)});
        }
        if (needRated_)
        {
            sink.push_back(monitor::DcmiSample{
                monitor::DcmiMetricKind::AICoreRatedFreq, tsNs, flat,
                mode_ == Mode::kInfo ? static_cast<double>(info.freq) : static_cast<double>(rated)});
        }
        return 0;
    }

   private:
    enum class Mode
    {
        kNone = 0,
        kInfo = 1,
        kFreq = 2
    };
    const DcmiApiLoader *api_ = nullptr;
    Mode mode_ = Mode::kNone;
    bool needCur_ = false;
    bool needRated_ = false;
};

// AICPU 层：最大/当前频率（aicpu_info 一次返回两值）
class AicpuInfoMetric : public DcmiMetricBase
{
   public:
    std::string GroupName() const override { return "AicpuInfo"; }
    std::vector<monitor::DcmiMetricKind> Provides() const override
    {
        return {monitor::DcmiMetricKind::AICPUMaxFreq, monitor::DcmiMetricKind::AICPUFreq};
    }
    bool Init(const DcmiApiLoader &api, const std::set<monitor::DcmiMetricKind> &enabled, const DcmiProbeDevice &probe,
              std::unordered_map<monitor::DcmiMetricKind, monitor::DcmiKindStatus> &status) override
    {
        api_ = &api;
        needMax_ = enabled.count(monitor::DcmiMetricKind::AICPUMaxFreq) > 0;
        needCur_ = enabled.count(monitor::DcmiMetricKind::AICPUFreq) > 0;
        if (!needMax_ && !needCur_)
        {
            return false;
        }
        if (!api.HasAicpuInfo())
        {
            detail::SetInactive(monitor::DcmiMetricKind::AICPUMaxFreq, "symbol missing", status);
            detail::SetInactive(monitor::DcmiMetricKind::AICPUFreq, "symbol missing", status);
            return false;
        }
        detail::DcmiAicpuInfo info{};
        if (probe.cardId >= 0 && api.GetAicpuInfo(probe.cardId, probe.devId, probe.flatDevId, &info) != 0)
        {
            detail::SetInactive(monitor::DcmiMetricKind::AICPUMaxFreq, "probe call failed", status);
            detail::SetInactive(monitor::DcmiMetricKind::AICPUFreq, "probe call failed", status);
            return false;
        }
        if (needMax_)
        {
            detail::SetActive(monitor::DcmiMetricKind::AICPUMaxFreq, status);
        }
        if (needCur_)
        {
            detail::SetActive(monitor::DcmiMetricKind::AICPUFreq, status);
        }
        return true;
    }
    int Collect(int cardId, int devId, uint32_t flatDevId, uint64_t tsNs, DcmiSampleSink &sink) override
    {
        detail::DcmiAicpuInfo info{};
        int ret = api_->GetAicpuInfo(cardId, devId, flatDevId, &info);
        if (ret != 0)
        {
            return ret;
        }
        int32_t flat = static_cast<int32_t>(flatDevId);
        if (needMax_)
        {
            sink.push_back(monitor::DcmiSample{monitor::DcmiMetricKind::AICPUMaxFreq, tsNs, flat,
                                               static_cast<double>(info.max_freq)});
        }
        if (needCur_)
        {
            sink.push_back(monitor::DcmiSample{monitor::DcmiMetricKind::AICPUFreq, tsNs, flat,
                                               static_cast<double>(info.cur_freq)});
        }
        return 0;
    }

   private:
    const DcmiApiLoader *api_ = nullptr;
    bool needMax_ = false;
    bool needCur_ = false;
};

// HBM 层：频率/容量/已用/带宽利用率/温度（hbm_info 一次返回五项）
class HbmInfoMetric : public DcmiMetricBase
{
   public:
    std::string GroupName() const override { return "HbmInfo"; }
    std::vector<monitor::DcmiMetricKind> Provides() const override
    {
        return {monitor::DcmiMetricKind::HBMFreq, monitor::DcmiMetricKind::HBMMemUsed,
                monitor::DcmiMetricKind::HBMMemTotal, monitor::DcmiMetricKind::HBMBandwidth,
                monitor::DcmiMetricKind::HBMTemp};
    }
    bool Init(const DcmiApiLoader &api, const std::set<monitor::DcmiMetricKind> &enabled, const DcmiProbeDevice &probe,
              std::unordered_map<monitor::DcmiMetricKind, monitor::DcmiKindStatus> &status) override
    {
        api_ = &api;
        if (!api.HasHbmInfo())
        {
            for (auto kind : Provides())
            {
                detail::SetInactive(kind, "symbol missing", status);
            }
            return false;
        }
        detail::DcmiHbmInfo info{};
        if (probe.cardId >= 0 && api.GetHbmInfo(probe.cardId, probe.devId, probe.flatDevId, &info) != 0)
        {
            for (auto kind : Provides())
            {
                detail::SetInactive(kind, "probe call failed", status);
            }
            return false;
        }
        for (auto kind : Provides())
        {
            if (enabled.count(kind) > 0)
            {
                detail::SetActive(kind, status);
            }
        }
        return true;
    }
    int Collect(int cardId, int devId, uint32_t flatDevId, uint64_t tsNs, DcmiSampleSink &sink) override
    {
        detail::DcmiHbmInfo info{};
        int ret = api_->GetHbmInfo(cardId, devId, flatDevId, &info);
        if (ret != 0)
        {
            return ret;
        }
        int32_t flat = static_cast<int32_t>(flatDevId);
        sink.push_back(
            monitor::DcmiSample{monitor::DcmiMetricKind::HBMFreq, tsNs, flat, static_cast<double>(info.freq)});
        sink.push_back(monitor::DcmiSample{monitor::DcmiMetricKind::HBMMemUsed, tsNs, flat,
                                           static_cast<double>(info.memory_usage)});
        sink.push_back(monitor::DcmiSample{monitor::DcmiMetricKind::HBMMemTotal, tsNs, flat,
                                           static_cast<double>(info.memory_size)});
        sink.push_back(monitor::DcmiSample{monitor::DcmiMetricKind::HBMBandwidth, tsNs, flat,
                                           static_cast<double>(info.bandwith_util_rate)});
        sink.push_back(
            monitor::DcmiSample{monitor::DcmiMetricKind::HBMTemp, tsNs, flat, static_cast<double>(info.temp)});
        return 0;
    }

   private:
    const DcmiApiLoader *api_ = nullptr;
};

// AICORE 层：AI Cube/Vector/AI Core/NPU 利用率（multi_utilization_rate 一次返回四项）
class MultiUtilMetric : public DcmiMetricBase
{
   public:
    std::string GroupName() const override { return "MultiUtil"; }
    std::vector<monitor::DcmiMetricKind> Provides() const override
    {
        return {monitor::DcmiMetricKind::AICoreUtil, monitor::DcmiMetricKind::AICubeUtil,
                monitor::DcmiMetricKind::VectorCoreUtil, monitor::DcmiMetricKind::NPUUtil};
    }
    bool Init(const DcmiApiLoader &api, const std::set<monitor::DcmiMetricKind> &enabled, const DcmiProbeDevice &probe,
              std::unordered_map<monitor::DcmiMetricKind, monitor::DcmiKindStatus> &status) override
    {
        api_ = &api;
        if (!api.HasMultiUtilization())
        {
            for (auto kind : Provides())
            {
                detail::SetInactive(kind, "symbol missing, fallback to per-type util_rate", status);
            }
            return false;
        }
        detail::DcmiMultiUtilizationInfo info{};
        if (probe.cardId >= 0 && api.GetMultiUtilization(probe.cardId, probe.devId, probe.flatDevId, &info) != 0)
        {
            for (auto kind : Provides())
            {
                detail::SetInactive(kind, "probe call failed", status);
            }
            return false;
        }
        for (auto kind : Provides())
        {
            if (enabled.count(kind) > 0)
            {
                detail::SetActive(kind, status);
            }
        }
        return true;
    }
    int Collect(int cardId, int devId, uint32_t flatDevId, uint64_t tsNs, DcmiSampleSink &sink) override
    {
        detail::DcmiMultiUtilizationInfo info{};
        int ret = api_->GetMultiUtilization(cardId, devId, flatDevId, &info);
        if (ret != 0)
        {
            return ret;
        }
        int32_t flat = static_cast<int32_t>(flatDevId);
        sink.push_back(monitor::DcmiSample{monitor::DcmiMetricKind::AICoreUtil, tsNs, flat,
                                           static_cast<double>(info.aicore_util)});
        sink.push_back(
            monitor::DcmiSample{monitor::DcmiMetricKind::AICubeUtil, tsNs, flat, static_cast<double>(info.aic_util)});
        sink.push_back(monitor::DcmiSample{monitor::DcmiMetricKind::VectorCoreUtil, tsNs, flat,
                                           static_cast<double>(info.aiv_util)});
        sink.push_back(
            monitor::DcmiSample{monitor::DcmiMetricKind::NPUUtil, tsNs, flat, static_cast<double>(info.npu_util)});
        return 0;
    }

   private:
    const DcmiApiLoader *api_ = nullptr;
};

// AICPU 层：AICPU 利用率（type=3）；multi_utilization 缺失时兜底接管 AICORE 利用率族。
// 该接口实测较慢（约 36ms/次），默认每 10 个节拍采样一次，避免拖垮采集节拍。
class UtilRateMetric : public DcmiMetricBase
{
   public:
    std::string GroupName() const override { return "UtilRate"; }
    uint32_t SampleEveryTicks() const override { return 10; }
    std::vector<monitor::DcmiMetricKind> Provides() const override
    {
        return {monitor::DcmiMetricKind::AICPUUtil, monitor::DcmiMetricKind::AICoreUtil,
                monitor::DcmiMetricKind::AICubeUtil, monitor::DcmiMetricKind::VectorCoreUtil,
                monitor::DcmiMetricKind::NPUUtil};
    }
    bool Init(const DcmiApiLoader &api, const std::set<monitor::DcmiMetricKind> &enabled, const DcmiProbeDevice &probe,
              std::unordered_map<monitor::DcmiMetricKind, monitor::DcmiKindStatus> &status) override
    {
        api_ = &api;
        if (!api.HasUtilizationRate())
        {
            for (auto kind : Provides())
            {
                detail::SetInactive(kind, "symbol missing", status);
            }
            return false;
        }
        for (auto kind : Provides())
        {
            if (enabled.count(kind) == 0)
            {
                continue;
            }
            int type = TypeOf(kind);
            if (type <= 0)
            {
                continue;
            }
            unsigned int raw = 0;
            if (probe.cardId >= 0 &&
                api.GetUtilizationRate(probe.cardId, probe.devId, probe.flatDevId, type, &raw) != 0)
            {
                detail::SetInactive(kind, "probe failed, util type not supported on this chip", status);
                continue;
            }
            activeKinds_.push_back(kind);
            detail::SetActive(kind, status);
        }
        return !activeKinds_.empty();
    }
    int Collect(int cardId, int devId, uint32_t flatDevId, uint64_t tsNs, DcmiSampleSink &sink) override
    {
        int lastRet = 0;
        for (auto kind : activeKinds_)
        {
            unsigned int raw = 0;
            int ret = api_->GetUtilizationRate(cardId, devId, flatDevId, TypeOf(kind), &raw);
            if (ret != 0)
            {
                lastRet = ret;
                continue;
            }
            sink.push_back(monitor::DcmiSample{kind, tsNs, static_cast<int32_t>(flatDevId), static_cast<double>(raw)});
        }
        return lastRet;
    }

   private:
    static int TypeOf(monitor::DcmiMetricKind kind)
    {
        switch (kind)
        {
            case monitor::DcmiMetricKind::AICPUUtil:
                return detail::UTIL_TYPE_AICPU;
            case monitor::DcmiMetricKind::AICoreUtil:
                return detail::UTIL_TYPE_AICORE;
            case monitor::DcmiMetricKind::AICubeUtil:
                return detail::UTIL_TYPE_AICUBE;
            case monitor::DcmiMetricKind::VectorCoreUtil:
                return detail::UTIL_TYPE_VECTOR;
            case monitor::DcmiMetricKind::NPUUtil:
                return detail::UTIL_TYPE_NPU;
            default:
                return -1;
        }
    }

    const DcmiApiLoader *api_ = nullptr;
    std::vector<monitor::DcmiMetricKind> activeKinds_;
};

}  // namespace dcmi
}  // namespace ipc_monitor
}  // namespace dynolog_npu

#endif  // MSMONITOR_DCMI_METRICS_H
