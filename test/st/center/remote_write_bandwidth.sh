#!/usr/bin/env bash
# Compare I3 lab throughput with both writers and with one writer stopped.
set -euo pipefail

stage=${1:-}
if [[ $# -ne 1 || ( "$stage" != smoke && "$stage" != target ) ]]; then
    printf '%s\n' 'Usage: bash remote_write_bandwidth.sh smoke|target' >&2
    exit 2
fi

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
tokens=${I3_TOKENS:-"$HOME/req2-final/tokens.json"}
log_dir="$HOME/req2-host-logs/i3-bandwidth-$(date +%Y%m%d-%H%M%S)-$$"
umask 077
mkdir -p -- "$log_dir"
printf 'Log directory: %s\n' "$log_dir"

load() {
    local label=$1 connections=$2 batch=$3 min_bytes=$4
    python3 "$here/remote_write_load.py" 127.0.0.1 18467 "$connections" 30 "$batch" \
        --tenant 10 --tokens "$tokens" --verify-readback --readback-timeout 600 \
        --min-steady-body-bytes-per-second "$min_bytes" | tee "$log_dir/$label.json"
}

if [[ "$stage" == smoke ]]; then
    load smoke 100 82 0
    exit 0
fi

ulimit -Sn 8192
for writer in req2-final-insert1 req2-final-insert2; do
    if [[ "$(docker inspect --format '{{.State.Running}}' "$writer")" != true ]]; then
        printf 'Writer %s must be running before the target test.\n' "$writer" >&2
        exit 1
    fi
done

load both 5100 82 9000000
(
    trap 'status=$?; docker start req2-final-insert1 || status=1; exit "$status"' EXIT
    docker stop req2-final-insert1
    docker inspect --format '{{.Name}} running={{.State.Running}}' \
        req2-final-insert1 req2-final-insert2 | tee "$log_dir/half-writers.txt"
    sleep 5
    load half 5100 82 9000000
)
