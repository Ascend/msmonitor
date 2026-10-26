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

// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "dynolog/src/Metrics.h"

#include <fmt/format.h>

#include <map>

namespace dynolog
{

const std::vector<MetricDesc> getAllMetrics()
{
    static std::vector<MetricDesc> metrics_ = {
        {.name = "kindName", .type = MetricType::Instant, .desc = "Report data kind name"},
        {.name = "duration", .type = MetricType::Delta, .desc = "Total execution time for corresponding kind"},
        {.name = "timestamp", .type = MetricType::Instant, .desc = "The timestamp of the reported data"},
        {.name = "deviceId", .type = MetricType::Instant, .desc = "The ID of the device for reporting data"},
    };
    return metrics_;
}

// These metrics are dynamic per network drive
const std::vector<MetricDesc> getNetworkMetrics()
{
    static std::vector<MetricDesc> metrics_ = {};
    return metrics_;
}

}  // namespace dynolog
