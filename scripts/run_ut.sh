#!/bin/bash
# Compatibility entry point. Use the language-specific scripts to run UTs separately.
set -euo pipefail

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    echo "Usage: bash scripts/run_ut.sh"
    exit 0
fi
if (( $# != 0 )); then
    echo "Usage: bash scripts/run_ut.sh" >&2
    exit 1
fi

SCRIPT_DIR=$(dirname "$(realpath "$0")")
bash "${SCRIPT_DIR}/run_ut_cpp.sh"
bash "${SCRIPT_DIR}/run_ut_python.sh"
