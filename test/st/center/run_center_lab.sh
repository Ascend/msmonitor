#!/usr/bin/env bash
# Run the center-only suites with synthetic clients. Run from a Linux Docker host.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$repo"
export I8_LAB_PROJECT="${I8_LAB_PROJECT:-msmonitor-center-final}"
export I8_LAB_IMAGE="${I8_LAB_IMAGE:-msmonitor-i8:final-lab}"
export I8_LAB_PORT_A="${I8_LAB_PORT_A:-18671}" I8_LAB_PORT_B="${I8_LAB_PORT_B:-18672}"
lab=deploy/center/control/lab.sh
case "${1:-i8}" in
i8)
    bash "$lab" up
    bash "$lab" test --verify-metadata --verify-commands --verify-summary
    bash test/st/center/restore_control_database.sh
    ;;
mixed)
    nodes="${2:-160}"
    tokens="${I8_LAB_STATE:-$HOME/.local/state/$I8_LAB_PROJECT}/secrets/tokens.json"
    origins=(--origin-a "http://127.0.0.1:$I8_LAB_PORT_A" --origin-b "http://127.0.0.1:$I8_LAB_PORT_B")
    for port in "$I8_LAB_PORT_A" "$I8_LAB_PORT_B"; do curl -fsS "http://127.0.0.1:$port/metrics" | grep '^msmonitor_i8_db_'; done
    python3 test/st/center/test_control_service_load.py "${origins[@]}" --tokens "$tokens" --daemons "$nodes" --cycles 4 --with-polls &
    load=$!
    if ! bash "$lab" test --verify-commands; then wait "$load" || true; exit 1; fi
    wait "$load"
    for port in "$I8_LAB_PORT_A" "$I8_LAB_PORT_B"; do curl -fsS "http://127.0.0.1:$port/metrics" | grep '^msmonitor_i8_db_'; done
    ;;
standalone)
    image="${CENTER_IMAGE:-msmonitor-center:acceptance}"
    docker build --target center -t "$image" deploy/center/control
    python3 test/st/center/test_standalone.py --image "$image"
    python3 test/st/center/test_writer_health.py --docker-fixture
    ;;
*) echo 'Usage: run_center_lab.sh i8 | mixed [nodes] | standalone' >&2; exit 2 ;;
esac
