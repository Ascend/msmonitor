// TEST-ONLY minimal gflags stub. The real gflags dependency is not part of the
// unit-test build. SimpleJsonServer only needs the certs_dir flag, so a plain
// process-wide std::string is enough to compile and link it.
#pragma once

#include <string>

#define DECLARE_string(name) extern std::string FLAGS_##name
#define DEFINE_string(name, default_value, ...) std::string FLAGS_##name = (default_value)
