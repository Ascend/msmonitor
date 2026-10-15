#!/bin/bash
# Build and run C++ UTs. Usage: bash scripts/run_ut_cpp.sh
# Environment: PYTHON (python3), JOBS (nproc), UT_BUILD_DIR (test/build_llt).
# UT_COVERAGE=ON is used by run_ut_coverage_cpp.sh; default OFF.
set -euo pipefail

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    head -n 4 "$(realpath "$0")"
    exit 0
fi
if (( $# != 0 )); then
    echo "Usage: bash scripts/run_ut_cpp.sh" >&2
    exit 1
fi

TOP_DIR=$(dirname "$(dirname "$(realpath "$0")")")
BUILD_DIR="${UT_BUILD_DIR:-${TOP_DIR}/test/build_llt}"
PYTHON="${PYTHON:-python3}"
UT_COVERAGE="${UT_COVERAGE:-OFF}"
if [[ "$UT_COVERAGE" != "ON" && "$UT_COVERAGE" != "OFF" ]]; then
    echo "ERROR: UT_COVERAGE must be ON or OFF" >&2
    exit 1
fi
PYTHON_EXECUTABLE=$(command -v "$PYTHON")

export LD_LIBRARY_PATH="${TOP_DIR}/plugin/stub${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
mkdir -p "$BUILD_DIR"
BUILD_DIR=$(realpath "$BUILD_DIR")
cmake -S "${TOP_DIR}/test" -B "$BUILD_DIR" \
    -DPACKAGE=ut -DCMAKE_BUILD_TYPE=Debug \
    -DPython_EXECUTABLE="$PYTHON_EXECUTABLE" -DENABLE_UT_COVERAGE="$UT_COVERAGE"
cmake --build "$BUILD_DIR" --parallel "${JOBS:-$(nproc)}"

if [[ "$UT_COVERAGE" == "ON" ]]; then
    # Keep gcno files for incremental builds, but never reuse old test counters.
    find "$BUILD_DIR" -name '*.gcda' -type f -delete
fi

TEST_DIR="${BUILD_DIR}/ut/plugin/ipc_monitor"
if [[ ! -d "$TEST_DIR" ]] || [[ -z "$(find "$TEST_DIR" -name 'test_*' -type f -executable -print -quit)" ]]; then
    echo "ERROR: No C++ test executables found under $TEST_DIR" >&2
    exit 1
fi

# CMake registers each UT with add_test(); preserve the existing serial execution.
cd "$BUILD_DIR"
ctest --output-on-failure --parallel 1
