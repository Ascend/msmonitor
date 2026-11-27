// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#pragma once

#include "StepAnalysis.h"

namespace msmonitor::process
{
void WriteChromeTrace(const std::vector<StepDataPtr> &steps, const std::filesystem::path &file);
void WriteMindStudioDb(const StepDataOutput &step, const std::filesystem::path &file);
}  // namespace msmonitor::process
