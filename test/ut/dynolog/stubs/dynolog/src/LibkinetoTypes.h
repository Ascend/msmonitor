// TEST-ONLY copy of the upstream dynolog header, which is absent from this
// repo. Fields mirror what LibkinetoConfigManager.cpp uses; not for reuse.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dynolog
{

enum class LibkinetoConfigType : int32_t
{
    EVENTS = 1,
    ACTIVITIES = 2
};

struct GpuProfilerResult
{
    std::vector<int32_t> processesMatched;
    std::vector<int32_t> eventProfilersTriggered;
    std::vector<int32_t> activityProfilersTriggered;
    int32_t eventProfilersBusy = 0;
    int32_t activityProfilersBusy = 0;
    std::vector<std::string> traceIds;
};

}  // namespace dynolog
