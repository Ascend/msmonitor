#!/usr/bin/env bash
# Run the isolated I8 Linux integration environment.
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd -- "$here/../../.." && pwd)
project="${I8_LAB_PROJECT:-msmonitor-i8-lab}"
if [[ ! "$project" =~ ^[a-z0-9][a-z0-9_-]*$ ]]; then
    printf 'Invalid I8_LAB_PROJECT: %s\n' "$project" >&2
    exit 2
fi
state="${I8_LAB_STATE:-$HOME/.local/state/$project}"
export I8_LAB_SECRETS="$state/secrets"
export I8_LAB_PORT_A="${I8_LAB_PORT_A:-18471}" I8_LAB_PORT_B="${I8_LAB_PORT_B:-18472}"
export I8_UID="$(id -u)" I8_GID="$(id -g)"

compose() {
    docker compose -p "$project" -f "$here/compose.yaml" "$@"
}

case "${1:-}" in
    up)
        docker compose version
        umask 077
        mkdir -p -- "$I8_LAB_SECRETS"
        chmod 700 -- "$I8_LAB_SECRETS"
        python3 - "$I8_LAB_SECRETS" <<'PY'
import json
import secrets
import sys
from pathlib import Path

directory = Path(sys.argv[1])
names = ("postgres_password", "database_url", "tokens.json")
present = [name for name in names if (directory / name).is_file()]
if present and len(present) != len(names):
    raise SystemExit(f"Incomplete credential files in {directory}. Restore the missing files.")
if not present:
    password = secrets.token_hex(24)
    values = (password, f"postgresql://i8:{password}@db:5432/i8",
              json.dumps({scope: secrets.token_hex(24) for scope in ("scope-a", "scope-b")}))
    for name, value in zip(names, values):
        with (directory / name).open("x", encoding="utf-8") as file:
            file.write(value + "\n")
PY
        compose up -d --build --wait --wait-timeout 120 app1 app2
        ;;
    test)
        shift
        exec python3 "$repo/test/st/center/test_control_service.py" \
            --origin-a "http://127.0.0.1:$I8_LAB_PORT_A" --origin-b "http://127.0.0.1:$I8_LAB_PORT_B" \
            --tokens "$I8_LAB_SECRETS/tokens.json" "$@"
        ;;
    compose)
        shift
        compose "$@"
        ;;
    *)
        printf '%s\n' 'Usage: bash lab.sh up | test [options] | compose COMMAND [args...]' >&2
        exit 2
        ;;
esac
