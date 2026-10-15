#!/bin/bash
# Run Python UTs without building the native extension.
# Usage: bash scripts/run_ut_python.sh
# Environment: PYTHON (default: python3). Installs xlsxwriter if missing.
set -euo pipefail

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    head -n 4 "$(realpath "$0")"
    exit 0
fi
if (( $# != 0 )); then
    echo "Usage: bash scripts/run_ut_python.sh" >&2
    exit 1
fi

TOP_DIR=$(dirname "$(dirname "$(realpath "$0")")")
TEST_DIR="${TOP_DIR}/test/ut/python"
PYTHON="${PYTHON:-python3}"

if [[ ! -d "$TEST_DIR" ]] || [[ -z "$(find "$TEST_DIR" -name 'test_*.py' -type f -print -quit)" ]]; then
    echo "ERROR: No Python unit tests found under $TEST_DIR" >&2
    exit 1
fi
if ! "$PYTHON" -c 'import xlsxwriter' >/dev/null 2>&1; then
    "$PYTHON" -m pip install xlsxwriter
fi

cd "$TOP_DIR"
export PYTHONPATH="${TOP_DIR}/plugin${PYTHONPATH:+:${PYTHONPATH}}"
"$PYTHON" -m unittest discover -s "$TEST_DIR" -p 'test_*.py' -v
