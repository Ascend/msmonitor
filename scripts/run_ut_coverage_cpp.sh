#!/bin/bash
# C++ UT coverage. Usage: bash scripts/run_ut_coverage_cpp.sh [diff]
# Environment: PYTHON (python3), JOBS (nproc), DIFF_BASE (origin/${TARGET_BRANCH:-${targetBranch:-master}}), FAIL_UNDER (80).
# Requires GCC/gcov, lcov >= 2.0, genhtml and C++ UT dependencies.
# diff also requires lcov_cobertura, diff-cover and a local Git base ref.
# Scope: plugin/ipc_monitor/ sources/headers compiled into UTs; excludes tests, stubs and third-party code.
# Reports: test/build_llt/output/cpp_coverage/{result,ut_report.info,cpp_coverage_report.log}
# With diff: coverage.xml, incremental.json and result/ut_incremental_coverage_report.html.
set -euo pipefail

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    head -n 8 "$(realpath "$0")"
    exit 0
fi
if (( $# > 1 )) || [[ -n "${1:-}" && "$1" != "diff" ]]; then
    echo "Usage: bash scripts/run_ut_coverage_cpp.sh [diff]" >&2
    exit 1
fi

SCRIPT_DIR=$(dirname "$(realpath "$0")")
TOP_DIR=$(dirname "$SCRIPT_DIR")
BUILD_DIR="${TOP_DIR}/test/build_llt/cpp_coverage_build"
COV_DIR="${TOP_DIR}/test/build_llt/output/cpp_coverage"
DIFF_BASE="${DIFF_BASE:-origin/${TARGET_BRANCH:-${targetBranch:-master}}}"
FAIL_UNDER="${FAIL_UNDER:-80}"

cd "$TOP_DIR"
for tool in lcov genhtml gcov; do
    command -v "$tool" >/dev/null || { echo "ERROR: Required tool not found: $tool" >&2; exit 1; }
done
LCOV_VERSION=$(lcov --version)
if [[ ! "$LCOV_VERSION" =~ ([0-9]+)\.([0-9]+) ]] || (( BASH_REMATCH[1] < 2 )); then
    echo "ERROR: lcov >= 2.0 is required; found: $LCOV_VERSION" >&2
    exit 1
fi
if [[ "${1:-}" == "diff" ]]; then
    for tool in lcov_cobertura diff-cover; do
        command -v "$tool" >/dev/null || { echo "ERROR: Required tool not found: $tool" >&2; exit 1; }
    done
    git merge-base HEAD "$DIFF_BASE" >/dev/null
fi

rm -rf -- "$COV_DIR"
mkdir -p "$COV_DIR"
UT_BUILD_DIR="$BUILD_DIR" UT_COVERAGE=ON bash "${SCRIPT_DIR}/run_ut_cpp.sh"
BUILD_DIR=$(realpath "$BUILD_DIR")
discover_coverage_dirs() {
    local counter object_path
    local object_root="$BUILD_DIR/ut/plugin/ipc_monitor/CMakeFiles"
    while IFS= read -r -d '' counter; do
        object_path="${counter#"$object_root/"}"
        case "${object_path#*.dir/}" in
            plugin/ipc_monitor/*|*/plugin/ipc_monitor/*|test_*.cpp.gcda)
                printf '%s\0' "${counter%/*}"
                ;;
        esac
    done < <(find "$object_root" -path '*/test_*.dir/*' -type f -name '*.gcda' -print0)
}

LCOV_DIR_ARGS=()
while IFS= read -r -d '' object_dir; do
    LCOV_DIR_ARGS+=(--directory "$object_dir")
done < <(discover_coverage_dirs | sort -zu)
if (( ${#LCOV_DIR_ARGS[@]} == 0 )); then
    echo "ERROR: No C++ UT coverage object directories found under $BUILD_DIR" >&2
    exit 1
fi
echo "Collecting C++ coverage from $((${#LCOV_DIR_ARGS[@]} / 2)) object directories"

# GCC 13 and lcov 2.0 record different exception tags for Connection.h templates.
LCOV_ARGS=("${LCOV_DIR_ARGS[@]}" --no-recursion --base-directory "$TOP_DIR"
    --no-external --branch-coverage
    --include "${TOP_DIR}/plugin/ipc_monitor/*" --ignore-errors unused,inconsistent,mismatch)
echo "Collecting executed C++ coverage..."
lcov --capture "${LCOV_ARGS[@]}" --output-file "${COV_DIR}/ut_report.info"
genhtml "${COV_DIR}/ut_report.info" --branch-coverage --ignore-errors inconsistent,mismatch \
    --output-directory "${COV_DIR}/result"
lcov --summary "${COV_DIR}/ut_report.info" --branch-coverage --ignore-errors inconsistent,mismatch \
    2>&1 | tee "${COV_DIR}/cpp_coverage_report.log"
echo "Full C++ coverage: ${COV_DIR}/result/index.html"

if [[ "${1:-}" == "diff" ]]; then
    lcov_cobertura "${COV_DIR}/ut_report.info" --base-dir "$TOP_DIR" -o "${COV_DIR}/coverage.xml"
    diff-cover "${COV_DIR}/coverage.xml" \
        --compare-branch="$DIFF_BASE" --include 'plugin/ipc_monitor/**' \
        --format "html:${COV_DIR}/result/ut_incremental_coverage_report.html,json:${COV_DIR}/incremental.json" \
        --fail-under="$FAIL_UNDER"
fi
