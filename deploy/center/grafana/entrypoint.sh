#!/bin/bash
set -eu

python3 -B /opt/msmonitor/server.py &
summary_pid=$!
/run.sh "$@" &
grafana_pid=$!

stop_children() {
  kill -TERM "$grafana_pid" "$summary_pid" 2>/dev/null || true
  wait "$grafana_pid" "$summary_pid" 2>/dev/null || true
}

trap 'stop_children; exit 0' TERM INT
status=0
wait -n "$grafana_pid" "$summary_pid" || status=$?
stop_children
exit "$status"
