/*
 * -------------------------------------------------------------------------
 * This file is part of the MindStudio project.
 * Copyright (c) 2026 Huawei Technologies Co.,Ltd.
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

#include <algorithm>
#include <cstddef>
#include <cstring>

#include "dynolog/src/LibkinetoConfigManager.h"

namespace dynolog
{

// Legacy plugins send the prefix before traceId; shorter payloads are rejected.
constexpr size_t kNpuStatusLegacySize = offsetof(NpuStatus, traceId);

// Copies at most one NpuStatus prefix; returns false for invalid payloads.
inline bool decodeNpuStatus(const void* buf, size_t size, NpuStatus& status)
{
    if (buf == nullptr || size < kNpuStatusLegacySize)
    {
        return false;
    }
    status = NpuStatus{};
    std::memcpy(&status, buf, std::min(size, sizeof(NpuStatus)));
    return true;
}

}  // namespace dynolog
