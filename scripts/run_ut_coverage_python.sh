#!/bin/bash
# Python UT coverage. Usage: bash scripts/run_ut_coverage_python.sh [diff]
# Environment: PYTHON (python3), DIFF_BASE (origin/${TARGET_BRANCH:-${targetBranch:-master}}), FAIL_UNDER (80).
# Installs missing Python dependencies; diff requires a local Git base ref.
# Scope: plugin/IPCMonitor/**/*.py (including files directly under IPCMonitor/).
# Reports: test/build_llt/output/python_coverage/{html,coverage.xml,python_coverage_report.log}
# With diff: inc_coverage_result.html and incremental.json in the same directory.
set -euo pipefail

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    head -n 7 "$(realpath "$0")"
    exit 0
fi
if (( $# > 1 )) || [[ -n "${1:-}" && "$1" != "diff" ]]; then
    echo "Usage: bash scripts/run_ut_coverage_python.sh [diff]" >&2
    exit 1
fi

TOP_DIR=$(dirname "$(dirname "$(realpath "$0")")")
COV_DIR="${TOP_DIR}/test/build_llt/output/python_coverage"
TEST_DIR="${TOP_DIR}/test/ut/python"
PYTHON="${PYTHON:-python3}"
DIFF_BASE="${DIFF_BASE:-origin/${TARGET_BRANCH:-${targetBranch:-master}}}"
FAIL_UNDER="${FAIL_UNDER:-80}"

cd "$TOP_DIR"
if ! "$PYTHON" -c 'import coverage, xlsxwriter' >/dev/null 2>&1; then
    "$PYTHON" -m pip install coverage xlsxwriter
fi
if [[ ! -d "$TEST_DIR" ]] || [[ -z "$(find "$TEST_DIR" -name 'test_*.py' -type f -print -quit)" ]]; then
    echo "ERROR: No Python unit tests found under $TEST_DIR" >&2
    exit 1
fi
if [[ "${1:-}" == "diff" ]]; then
    if ! "$PYTHON" -c 'import diff_cover.diff_cover_tool' >/dev/null 2>&1; then
        "$PYTHON" -m pip install diff-cover
    fi
    git merge-base HEAD "$DIFF_BASE" >/dev/null
fi

# Only reset this script's reports, leaving C++ results untouched.
rm -rf -- "$COV_DIR"
mkdir -p "$COV_DIR"
export PYTHONPATH="${TOP_DIR}/plugin${PYTHONPATH:+:${PYTHONPATH}}"
export COVERAGE_FILE="${COV_DIR}/.coverage"
# Keep the source setting available to XML generation for correct Git paths.
cat > "${COV_DIR}/coverage.rc" <<'EOF'
[run]
branch = True
relative_files = True
source = plugin/IPCMonitor
EOF
export COVERAGE_RCFILE="${COV_DIR}/coverage.rc"

"$PYTHON" -m coverage run -m unittest discover -s "$TEST_DIR" -p 'test_*.py' -v
"$PYTHON" -m coverage xml -o "${COV_DIR}/coverage.xml"
"$PYTHON" -m coverage html -d "${COV_DIR}/html"
"$PYTHON" -m coverage report -m | tee "${COV_DIR}/python_coverage_report.log"
echo "Full Python coverage: ${COV_DIR}/html/index.html"

if [[ "${1:-}" == "diff" ]]; then
    "$PYTHON" -m diff_cover.diff_cover_tool "${COV_DIR}/coverage.xml" \
        --compare-branch="$DIFF_BASE" --include 'plugin/IPCMonitor/**/*.py' \
        --format "html:${COV_DIR}/inc_coverage_result.html,json:${COV_DIR}/incremental.json" \
        --fail-under="$FAIL_UNDER"
fi
