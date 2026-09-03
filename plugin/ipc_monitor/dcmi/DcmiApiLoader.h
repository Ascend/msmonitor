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

#ifndef MSMONITOR_DCMI_API_LOADER_H
#define MSMONITOR_DCMI_API_LOADER_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "dcmi/DcmiTypes.h"

namespace dynolog_npu
{
namespace ipc_monitor
{
namespace dcmi
{

// DCMI v1/v2 函数指针集合，dlopen/dlsym 一次性预取，采集期零解析。
// v1 为 (card_id, device_id) 双参模型；v2 为扁平 dev_id 单参模型（950 等 v2-only 代际）。
struct DcmiApiFuncs
{
    using fn_dcmi_init = int (*)(void);
    using fn_dcmi_get_card_list = int (*)(int *, int *, int);
    using fn_dcmi_get_device_id_in_card = int (*)(int, int *, int *, int *);
    using fn_dcmi_get_device_power_info = int (*)(int, int, int *);
    using fn_dcmi_get_device_temperature = int (*)(int, int, int *);
    using fn_dcmi_get_device_aicore_info = int (*)(int, int, void *);
    using fn_dcmi_get_device_aicpu_info = int (*)(int, int, void *);
    using fn_dcmi_get_device_hbm_info = int (*)(int, int, void *);
    using fn_dcmi_get_device_multi_utilization_rate = int (*)(int, int, void *);
    using fn_dcmi_get_device_utilization_rate = int (*)(int, int, int, unsigned int *);
    using fn_dcmi_get_device_frequency = int (*)(int, int, int, unsigned int *);
    using fn_dcmi_get_dcmi_version = int (*)(unsigned int *);

    fn_dcmi_init dcmi_init = nullptr;
    fn_dcmi_get_card_list dcmi_get_card_list = nullptr;
    fn_dcmi_get_device_id_in_card dcmi_get_device_id_in_card = nullptr;
    fn_dcmi_get_device_power_info dcmi_get_device_power_info = nullptr;
    fn_dcmi_get_device_temperature dcmi_get_device_temperature = nullptr;
    fn_dcmi_get_device_aicore_info dcmi_get_device_aicore_info = nullptr;
    fn_dcmi_get_device_aicpu_info dcmi_get_device_aicpu_info = nullptr;
    fn_dcmi_get_device_hbm_info dcmi_get_device_hbm_info = nullptr;
    fn_dcmi_get_device_multi_utilization_rate dcmi_get_device_multi_utilization_rate = nullptr;
    fn_dcmi_get_device_utilization_rate dcmi_get_device_utilization_rate = nullptr;
    fn_dcmi_get_device_frequency dcmi_get_device_frequency = nullptr;
    fn_dcmi_get_dcmi_version dcmi_get_dcmi_version = nullptr;

    using fn_dcmiv2_init = int (*)(void);
    using fn_dcmiv2_get_device_list = int (*)(int *, int *, int);
    using fn_dcmiv2_get_all_device_count = int (*)(int *);
    using fn_dcmiv2_get_device_power_info = int (*)(int, int *);
    using fn_dcmiv2_get_device_temperature = int (*)(int, int *);
    using fn_dcmiv2_get_device_frequency = int (*)(int, int, unsigned int *);
    using fn_dcmiv2_get_device_utilization_rate = int (*)(int, int, unsigned int *);
    using fn_dcmiv2_get_device_aicore_info = int (*)(int, void *);
    using fn_dcmiv2_get_device_aicpu_info = int (*)(int, void *);
    using fn_dcmiv2_get_device_hbm_info = int (*)(int, void *);
    using fn_dcmiv2_get_device_multi_utilization_rate = int (*)(int, void *);

    fn_dcmiv2_init dcmiv2_init = nullptr;
    fn_dcmiv2_get_device_list dcmiv2_get_device_list = nullptr;
    fn_dcmiv2_get_all_device_count dcmiv2_get_all_device_count = nullptr;
    fn_dcmiv2_get_device_power_info dcmiv2_get_device_power_info = nullptr;
    fn_dcmiv2_get_device_temperature dcmiv2_get_device_temperature = nullptr;
    fn_dcmiv2_get_device_frequency dcmiv2_get_device_frequency = nullptr;
    fn_dcmiv2_get_device_utilization_rate dcmiv2_get_device_utilization_rate = nullptr;
    fn_dcmiv2_get_device_aicore_info dcmiv2_get_device_aicore_info = nullptr;
    fn_dcmiv2_get_device_aicpu_info dcmiv2_get_device_aicpu_info = nullptr;
    fn_dcmiv2_get_device_hbm_info dcmiv2_get_device_hbm_info = nullptr;
    fn_dcmiv2_get_device_multi_utilization_rate dcmiv2_get_device_multi_utilization_rate = nullptr;
};

// DCMI 库加载器：dlopen 一次性加载 + dlsym 预取 + 拓扑枚举。
// 版本策略：优先 v2（新代际主接口，950 等），v2 符号缺失/init 失败/枚举失败自动回退 v1；
// v1-only 库（910B 等）无 dcmiv2 符号，直接走 v1。
//
// 扩展点：GetXxx/HasXxx 统一访问器按版本分派，metric 层无代际分支；
// 新增 DCMI 版本/接口只需在此维护分派，metric 与导出层零改动。
class DcmiApiLoader
{
   public:
    DcmiApiLoader() = default;
    ~DcmiApiLoader();

    DcmiApiLoader(const DcmiApiLoader &) = delete;
    DcmiApiLoader &operator=(const DcmiApiLoader &) = delete;

    monitor::DcmiApiStatus Init();
    // UT 注入：跳过 dlopen/dlsym，直接给定符号表与拓扑
    monitor::DcmiApiStatus InjectForTest(const DcmiApiFuncs &funcs, int cardNum, int devNumPerCard);
    // UT 注入：v2 模式（扁平模型），拓扑经 dcmiv2_get_device_list 假函数枚举（覆盖 v2 分支）
    monitor::DcmiApiStatus InjectForTestV2(const DcmiApiFuncs &funcs);

    bool Available() const;
    bool IsV2() const { return v2Mode_; }
    const DcmiApiFuncs &Funcs() const { return funcs_; }
    const monitor::DcmiLoadInfo &LoadInfo() const { return loadInfo_; }

    // ---- 统一采集访问器：v1 用 (cardId, dcmiDevId)，v2 用 flatDevId，内部自动分派 ----
    int GetPower(int cardId, int devId, uint32_t flatDevId, int *out) const;
    int GetTemperature(int cardId, int devId, uint32_t flatDevId, int *out) const;
    int GetFrequency(int cardId, int devId, uint32_t flatDevId, int type, unsigned int *out) const;
    int GetUtilizationRate(int cardId, int devId, uint32_t flatDevId, int type, unsigned int *out) const;
    int GetAicoreInfo(int cardId, int devId, uint32_t flatDevId, void *out) const;
    int GetAicpuInfo(int cardId, int devId, uint32_t flatDevId, void *out) const;
    int GetHbmInfo(int cardId, int devId, uint32_t flatDevId, void *out) const;
    int GetMultiUtilization(int cardId, int devId, uint32_t flatDevId, void *out) const;

    // ---- 接口可用性（metric Init 阶段校验）----
    bool HasPower() const;
    bool HasTemperature() const;
    bool HasFrequency() const;
    bool HasUtilizationRate() const;
    bool HasAicoreInfo() const;
    bool HasAicpuInfo() const;
    bool HasHbmInfo() const;
    bool HasMultiUtilization() const;

    bool MapDevId(uint32_t flatDevId, int &cardId, int &dcmiDevId) const;
    std::vector<uint32_t> ProbeAllDevices() const;
    // 经 ACL aclrtGetDevice 取当前进程在用设备（取不到返回 false，回退全卡）
    static bool GetCurrentDevice(int32_t &devId);
    void SetDevices(const std::vector<uint32_t> &devices);

   private:
    monitor::DcmiApiStatus EnumerateTopology();

    void *handle_ = nullptr;
    bool injected_ = false;
    bool v2Mode_ = false;
    DcmiApiFuncs funcs_;
    monitor::DcmiLoadInfo loadInfo_;
    std::vector<std::pair<int, int>> flatToCardChip_;  // flat → (cardId, chipId)
    int cardNum_ = 0;
    int devNumPerCard_ = 0;
};

}  // namespace dcmi
}  // namespace ipc_monitor
}  // namespace dynolog_npu

#endif  // MSMONITOR_DCMI_API_LOADER_H
