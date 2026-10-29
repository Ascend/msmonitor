// TEST-ONLY minimal hbt System stub (Linux-only, like the rest of this tree).
#pragma once

#include <unistd.h>

#include <string>

namespace facebook
{
namespace hbt
{

inline std::string getHostName()
{
    char hostname[256] = {};
    if (gethostname(hostname, sizeof(hostname) - 1) != 0)
    {
        return "unknown";
    }
    return hostname;
}

}  // namespace hbt
}  // namespace facebook
