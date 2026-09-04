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

#include "dcmi/DcmiApiLoader.h"

#include <dlfcn.h>
#include <glog/logging.h>

namespace dynolog_npu
{
namespace ipc_monitor
{
namespace dcmi
{

namespace
{
constexpr int DCMI_OK = 0;
constexpr int MAX_CARD_NUM = 64;
constexpr const char *LIBDCMI_CANDIDATES[] = {
    "libdcmi.so",  // 标准搜索路径（含 LD_LIBRARY_PATH）
    "/usr/local/dcmi/lib64/libdcmi.so",
    "/usr/local/dcmi/lib/libdcmi.so",
    "/usr/local/Ascend/driver/lib64/driver/libdcmi.so",
    "/usr/local/Ascend/driver/lib/libdcmi.so",
    "/usr/lib64/libdcmi.so",
    "/usr/lib/libdcmi.so",
};
constexpr int LIBDCMI_CANDIDATE_NUM = static_cast<int>(sizeof(LIBDCMI_CANDIDATES) / sizeof(LIBDCMI_CANDIDATES[0]));
}  // namespace

DcmiApiLoader::~DcmiApiLoader()
{
    if (handle_ != nullptr)
    {
        dlclose(handle_);
        handle_ = nullptr;
    }
}

bool DcmiApiLoader::Available() const
{
    if (!injected_ && handle_ == nullptr)
    {
        return false;
    }
    return funcs_.dcmi_init != nullptr || funcs_.dcmiv2_init != nullptr;
}

monitor::DcmiApiStatus DcmiApiLoader::Init()
{
    if (loadInfo_.status == monitor::DcmiApiStatus::OK)
    {
        return monitor::DcmiApiStatus::OK;  // 已初始化（含测试注入）
    }
    loadInfo_ = monitor::DcmiLoadInfo{};
    for (int i = 0; i < LIBDCMI_CANDIDATE_NUM; ++i)
    {
        void *handle = dlopen(LIBDCMI_CANDIDATES[i], RTLD_LAZY | RTLD_NODELETE);
        if (handle != nullptr)
        {
            handle_ = handle;
            loadInfo_.libPath = LIBDCMI_CANDIDATES[i];
            break;
        }
    }
    if (handle_ == nullptr)
    {
        loadInfo_.status = monitor::DcmiApiStatus::NOT_SUPPORT;
        loadInfo_.error = "libdcmi.so unavailable: " + std::string(dlerror());
        LOG(WARNING) << "DcmiApiLoader: " << loadInfo_.error;
        return loadInfo_.status;
    }

#define LOAD_SYM(name)                                                                  \
    do                                                                                  \
    {                                                                                   \
        funcs_.name = reinterpret_cast<DcmiApiFuncs::fn_##name>(dlsym(handle_, #name)); \
        if (funcs_.name == nullptr)                                                     \
        {                                                                               \
            loadInfo_.missingSymbols.emplace_back(#name);                               \
        }                                                                               \
    } while (0)
    LOAD_SYM(dcmi_init);
    LOAD_SYM(dcmi_get_card_list);
    LOAD_SYM(dcmi_get_device_id_in_card);
    LOAD_SYM(dcmi_get_device_power_info);
    LOAD_SYM(dcmi_get_device_temperature);
    LOAD_SYM(dcmi_get_device_aicore_info);
    LOAD_SYM(dcmi_get_device_aicpu_info);
    LOAD_SYM(dcmi_get_device_hbm_info);
    LOAD_SYM(dcmi_get_device_multi_utilization_rate);
    LOAD_SYM(dcmi_get_device_utilization_rate);
    LOAD_SYM(dcmi_get_device_frequency);
    LOAD_SYM(dcmi_get_dcmi_version);
    LOAD_SYM(dcmiv2_init);
    LOAD_SYM(dcmiv2_get_device_list);
    LOAD_SYM(dcmiv2_get_all_device_count);
    LOAD_SYM(dcmiv2_get_device_power_info);
    LOAD_SYM(dcmiv2_get_device_temperature);
    LOAD_SYM(dcmiv2_get_device_frequency);
    LOAD_SYM(dcmiv2_get_device_utilization_rate);
    LOAD_SYM(dcmiv2_get_device_aicore_info);
    LOAD_SYM(dcmiv2_get_device_aicpu_info);
    LOAD_SYM(dcmiv2_get_device_hbm_info);
    LOAD_SYM(dcmiv2_get_device_multi_utilization_rate);
#undef LOAD_SYM

    // ---- 版本选择：优先 v2（新代际主接口，950 等）；v2 不可用/失败再回退 v1 ----
    // v1-only 库（910B 等）无 dcmiv2 符号，此处直接跳过，零开销回退 v1。
    bool v2Attempted = false;
    std::string v2Fail;
    if (funcs_.dcmiv2_init != nullptr)
    {
        v2Attempted = true;
        if (funcs_.dcmiv2_get_device_list == nullptr)
        {
            v2Fail = "dcmiv2_init present but dcmiv2_get_device_list missing";
        }
        else
        {
            int ret = funcs_.dcmiv2_init();
            if (ret == DCMI_OK)
            {
                v2Mode_ = true;
                loadInfo_.version = "V2";
                if (EnumerateTopology() == monitor::DcmiApiStatus::OK)
                {
                    loadInfo_.status = monitor::DcmiApiStatus::OK;
                    LOG(INFO) << "DcmiApiLoader init ok (V2), lib=" << loadInfo_.libPath << " deviceNum=" << cardNum_;
                    return loadInfo_.status;
                }
                v2Mode_ = false;  // v2 枚举失败 → 回退 v1
                v2Fail = loadInfo_.error;
            }
            else
            {
                v2Fail = "dcmiv2_init failed, ret=" + std::to_string(ret);
            }
        }
    }

    if (funcs_.dcmi_init != nullptr && funcs_.dcmi_get_card_list != nullptr &&
        funcs_.dcmi_get_device_id_in_card != nullptr)
    {
        int ret = funcs_.dcmi_init();
        if (ret == DCMI_OK)
        {
            loadInfo_.version = "V1";
            if (EnumerateTopology() == monitor::DcmiApiStatus::OK)
            {
                loadInfo_.status = monitor::DcmiApiStatus::OK;
                LOG(INFO) << "DcmiApiLoader init ok (V1), lib=" << loadInfo_.libPath << " cardNum=" << cardNum_
                          << " devNumPerCard=" << devNumPerCard_;
                return loadInfo_.status;
            }
            loadInfo_.status = monitor::DcmiApiStatus::FAILED;
            loadInfo_.error = "dcmi enumeration failed: " + loadInfo_.error;
            LOG(WARNING) << "DcmiApiLoader: " << loadInfo_.error;
            return loadInfo_.status;
        }
        loadInfo_.initRet = ret;
        std::string v1Fail = "dcmi_init failed, ret=" + std::to_string(ret) +
                             " (DCMI_ERR_CODE_NOT_SUPPORT: not supported by this lib/driver)";
        loadInfo_.status = monitor::DcmiApiStatus::FAILED;
        loadInfo_.error = v2Attempted ? "DCMI unavailable: v2(" + v2Fail + "); v1(" + v1Fail + ")" : v1Fail;
        LOG(WARNING) << "DcmiApiLoader: " << loadInfo_.error << " (lib=" << loadInfo_.libPath << ")";
        return loadInfo_.status;
    }

    loadInfo_.status = monitor::DcmiApiStatus::NOT_SUPPORT;
    loadInfo_.error = "no usable DCMI v1/v2 init symbols (lib=" + loadInfo_.libPath + ")";
    if (v2Attempted)
    {
        loadInfo_.error += "; v2: " + v2Fail;
    }
    std::string missing;
    for (const auto &sym : loadInfo_.missingSymbols)
    {
        missing += sym + ",";
    }
    if (!missing.empty())
    {
        loadInfo_.error += " missing=[" + missing.substr(0, missing.size() - 1) + "]";
    }
    LOG(WARNING) << "DcmiApiLoader: " << loadInfo_.error;
    return loadInfo_.status;
}

monitor::DcmiApiStatus DcmiApiLoader::EnumerateTopology()
{
    if (v2Mode_)
    {
        // v2：dcmiv2_get_device_list 直接返回扁平设备列表
        std::vector<int> devList(MAX_CARD_NUM * 4, 0);
        int cnt = 0;
        int ret = funcs_.dcmiv2_get_device_list(devList.data(), &cnt, static_cast<int>(devList.size()));
        if (ret != DCMI_OK || cnt <= 0 || cnt > MAX_CARD_NUM * 4)
        {
            loadInfo_.status = monitor::DcmiApiStatus::FAILED;
            loadInfo_.error =
                "dcmiv2_get_device_list failed, ret=" + std::to_string(ret) + " cnt=" + std::to_string(cnt);
            LOG(WARNING) << "DcmiApiLoader: " << loadInfo_.error;
            return loadInfo_.status;
        }
        cardNum_ = cnt;
        devNumPerCard_ = 1;
        loadInfo_.cardNum = cnt;
        loadInfo_.deviceNumPerCard = 1;
        flatToCardChip_.clear();
        for (int i = 0; i < cnt; ++i)
        {
            flatToCardChip_.emplace_back(0, devList[i]);  // v2 扁平：flatDevId == 设备号
        }
        loadInfo_.error.clear();
        return monitor::DcmiApiStatus::OK;
    }

    // v1：(card_id, device_id) 两级模型
    std::vector<int> cardList(MAX_CARD_NUM, 0);
    int num = 0;
    int ret = funcs_.dcmi_get_card_list(&num, cardList.data(), MAX_CARD_NUM);
    if (ret != DCMI_OK || num <= 0 || num > MAX_CARD_NUM)
    {
        loadInfo_.status = monitor::DcmiApiStatus::FAILED;
        loadInfo_.error = "dcmi_get_card_list failed, ret=" + std::to_string(ret) + " num=" + std::to_string(num);
        LOG(WARNING) << "DcmiApiLoader: " << loadInfo_.error;
        return loadInfo_.status;
    }
    cardNum_ = num;
    loadInfo_.cardNum = num;

    flatToCardChip_.clear();
    uint32_t flat = 0;
    int maxDevPerCard = 0;
    for (int i = 0; i < cardNum_; ++i)
    {
        int devMax = 0;
        int mcuId = -1;
        int cpuId = -1;
        ret = funcs_.dcmi_get_device_id_in_card(cardList[i], &devMax, &mcuId, &cpuId);
        if (ret != DCMI_OK || devMax <= 0)
        {
            LOG(WARNING) << "DcmiApiLoader: dcmi_get_device_id_in_card failed, card=" << cardList[i] << " ret=" << ret
                         << ", skipped";
            continue;
        }
        for (int chip = 0; chip < devMax; ++chip)
        {
            flatToCardChip_.emplace_back(cardList[i], chip);
            ++flat;
        }
        if (devMax > maxDevPerCard)
        {
            maxDevPerCard = devMax;
        }
    }
    devNumPerCard_ = maxDevPerCard;
    loadInfo_.deviceNumPerCard = maxDevPerCard;
    if (flatToCardChip_.empty())
    {
        loadInfo_.status = monitor::DcmiApiStatus::FAILED;
        loadInfo_.error = "no NPU device enumerated";
        LOG(WARNING) << "DcmiApiLoader: " << loadInfo_.error;
        return loadInfo_.status;
    }
    loadInfo_.error.clear();
    return monitor::DcmiApiStatus::OK;
}

monitor::DcmiApiStatus DcmiApiLoader::InjectForTest(const DcmiApiFuncs &funcs, int cardNum, int devNumPerCard)
{
    injected_ = true;
    v2Mode_ = false;
    funcs_ = funcs;
    cardNum_ = cardNum;
    devNumPerCard_ = devNumPerCard;
    loadInfo_ = monitor::DcmiLoadInfo{};
    loadInfo_.status = monitor::DcmiApiStatus::OK;
    loadInfo_.version = "V1";
    loadInfo_.libPath = "test";
    loadInfo_.cardNum = cardNum;
    loadInfo_.deviceNumPerCard = devNumPerCard;
    flatToCardChip_.clear();
    for (int i = 0; i < cardNum * devNumPerCard; ++i)
    {
        flatToCardChip_.emplace_back(i / devNumPerCard, i % devNumPerCard);
    }
    return monitor::DcmiApiStatus::OK;
}

monitor::DcmiApiStatus DcmiApiLoader::InjectForTestV2(const DcmiApiFuncs &funcs)
{
    injected_ = true;
    v2Mode_ = true;
    funcs_ = funcs;
    loadInfo_ = monitor::DcmiLoadInfo{};
    loadInfo_.version = "V2";
    loadInfo_.libPath = "test";
    flatToCardChip_.clear();
    monitor::DcmiApiStatus st = EnumerateTopology();
    if (st != monitor::DcmiApiStatus::OK)
    {
        return st;
    }
    loadInfo_.status = monitor::DcmiApiStatus::OK;
    return loadInfo_.status;
}

bool DcmiApiLoader::MapDevId(uint32_t flatDevId, int &cardId, int &dcmiDevId) const
{
    if (flatDevId >= flatToCardChip_.size())
    {
        return false;
    }
    cardId = flatToCardChip_[flatDevId].first;
    dcmiDevId = flatToCardChip_[flatDevId].second;
    return true;
}

std::vector<uint32_t> DcmiApiLoader::ProbeAllDevices() const
{
    std::vector<uint32_t> out;
    for (uint32_t flat = 0; flat < flatToCardChip_.size(); ++flat)
    {
        out.push_back(flat);
    }
    return out;
}

void DcmiApiLoader::SetDevices(const std::vector<uint32_t> &devices) { loadInfo_.devices = devices; }

// ---- 统一访问器：v2 用扁平 dev_id，v1 用 (card_id, device_id) ----

int DcmiApiLoader::GetPower(int cardId, int devId, uint32_t flatDevId, int *out) const
{
    if (v2Mode_)
    {
        return funcs_.dcmiv2_get_device_power_info(static_cast<int>(flatDevId), out);
    }
    return funcs_.dcmi_get_device_power_info(cardId, devId, out);
}

int DcmiApiLoader::GetTemperature(int cardId, int devId, uint32_t flatDevId, int *out) const
{
    if (v2Mode_)
    {
        return funcs_.dcmiv2_get_device_temperature(static_cast<int>(flatDevId), out);
    }
    return funcs_.dcmi_get_device_temperature(cardId, devId, out);
}

int DcmiApiLoader::GetFrequency(int cardId, int devId, uint32_t flatDevId, int type, unsigned int *out) const
{
    if (v2Mode_)
    {
        return funcs_.dcmiv2_get_device_frequency(static_cast<int>(flatDevId), type, out);
    }
    return funcs_.dcmi_get_device_frequency(cardId, devId, type, out);
}

int DcmiApiLoader::GetUtilizationRate(int cardId, int devId, uint32_t flatDevId, int type, unsigned int *out) const
{
    if (v2Mode_)
    {
        return funcs_.dcmiv2_get_device_utilization_rate(static_cast<int>(flatDevId), type, out);
    }
    return funcs_.dcmi_get_device_utilization_rate(cardId, devId, type, out);
}

int DcmiApiLoader::GetAicoreInfo(int cardId, int devId, uint32_t flatDevId, void *out) const
{
    if (v2Mode_)
    {
        return funcs_.dcmiv2_get_device_aicore_info(static_cast<int>(flatDevId), out);
    }
    return funcs_.dcmi_get_device_aicore_info(cardId, devId, out);
}

int DcmiApiLoader::GetAicpuInfo(int cardId, int devId, uint32_t flatDevId, void *out) const
{
    if (v2Mode_)
    {
        return funcs_.dcmiv2_get_device_aicpu_info(static_cast<int>(flatDevId), out);
    }
    return funcs_.dcmi_get_device_aicpu_info(cardId, devId, out);
}

int DcmiApiLoader::GetHbmInfo(int cardId, int devId, uint32_t flatDevId, void *out) const
{
    if (v2Mode_)
    {
        return funcs_.dcmiv2_get_device_hbm_info(static_cast<int>(flatDevId), out);
    }
    return funcs_.dcmi_get_device_hbm_info(cardId, devId, out);
}

int DcmiApiLoader::GetMultiUtilization(int cardId, int devId, uint32_t flatDevId, void *out) const
{
    if (v2Mode_)
    {
        return funcs_.dcmiv2_get_device_multi_utilization_rate(static_cast<int>(flatDevId), out);
    }
    return funcs_.dcmi_get_device_multi_utilization_rate(cardId, devId, out);
}

bool DcmiApiLoader::HasPower() const
{
    return v2Mode_ ? funcs_.dcmiv2_get_device_power_info != nullptr : funcs_.dcmi_get_device_power_info != nullptr;
}

bool DcmiApiLoader::HasTemperature() const
{
    return v2Mode_ ? funcs_.dcmiv2_get_device_temperature != nullptr : funcs_.dcmi_get_device_temperature != nullptr;
}

bool DcmiApiLoader::HasFrequency() const
{
    return v2Mode_ ? funcs_.dcmiv2_get_device_frequency != nullptr : funcs_.dcmi_get_device_frequency != nullptr;
}

bool DcmiApiLoader::HasUtilizationRate() const
{
    return v2Mode_ ? funcs_.dcmiv2_get_device_utilization_rate != nullptr
                   : funcs_.dcmi_get_device_utilization_rate != nullptr;
}

bool DcmiApiLoader::HasAicoreInfo() const
{
    return v2Mode_ ? funcs_.dcmiv2_get_device_aicore_info != nullptr : funcs_.dcmi_get_device_aicore_info != nullptr;
}

bool DcmiApiLoader::HasAicpuInfo() const
{
    return v2Mode_ ? funcs_.dcmiv2_get_device_aicpu_info != nullptr : funcs_.dcmi_get_device_aicpu_info != nullptr;
}

bool DcmiApiLoader::HasHbmInfo() const
{
    return v2Mode_ ? funcs_.dcmiv2_get_device_hbm_info != nullptr : funcs_.dcmi_get_device_hbm_info != nullptr;
}

bool DcmiApiLoader::HasMultiUtilization() const
{
    return v2Mode_ ? funcs_.dcmiv2_get_device_multi_utilization_rate != nullptr
                   : funcs_.dcmi_get_device_multi_utilization_rate != nullptr;
}

bool DcmiApiLoader::GetCurrentDevice(int32_t &devId)
{
    // 优先 dlopen(NULL) 全局符号表查 aclrtGetDevice（torch_npu 已加载 libascendcl）
    using fn_t = int (*)(int32_t *);
    void *handle = dlopen(nullptr, RTLD_LAZY);
    fn_t fn = (handle != nullptr) ? reinterpret_cast<fn_t>(dlsym(handle, "aclrtGetDevice")) : nullptr;
    if (handle != nullptr)
    {
        dlclose(handle);  // 主程序句柄，dlclose 不卸载
    }
    if (fn != nullptr)
    {
        int32_t id = -1;
        int ret = fn(&id);  // aclError: 0 == ACL_SUCCESS
        if (ret == 0 && id >= 0)
        {
            devId = id;
            return true;
        }
        return false;
    }
    // 兜底：显式加载 libascendcl.so 候选路径（进程可能以 RTLD_LOCAL 加载）
    static const char *kAscendclCandidates[] = {
        "libascendcl.so",
        "/usr/local/Ascend/ascend-toolkit/latest/lib64/libascendcl.so",
        "/usr/local/Ascend/ascend-toolkit/latest/aarch64-linux/lib64/libascendcl.so",
        "/usr/local/Ascend/ascend-toolkit/latest/x86_64-linux/lib64/libascendcl.so",
    };
    for (const char *path : kAscendclCandidates)
    {
        void *h = dlopen(path, RTLD_LAZY | RTLD_NODELETE);
        if (h == nullptr)
        {
            continue;
        }
        fn = reinterpret_cast<fn_t>(dlsym(h, "aclrtGetDevice"));
        if (fn == nullptr)
        {
            dlclose(h);
            continue;
        }
        int32_t id = -1;
        int ret = fn(&id);
        dlclose(h);  // RTLD_NODELETE 下库保持驻留
        if (ret == 0 && id >= 0)
        {
            devId = id;
            return true;
        }
        return false;
    }
    LOG(WARNING) << "DcmiApiLoader: aclrtGetDevice unavailable, fallback to all visible devices";
    return false;
}

}  // namespace dcmi
}  // namespace ipc_monitor
}  // namespace dynolog_npu
