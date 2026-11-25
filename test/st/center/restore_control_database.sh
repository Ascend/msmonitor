#!/usr/bin/env bash
# Verify a logical backup using a fresh database inside the I8 Compose lab.
set -euo pipefail
lab=$(cd "$(dirname "$0")/../../.." && pwd)/deploy/center/control/lab.sh
db() { bash "$lab" compose exec -T db "$@"; }
scratch="i8_restore_$(date +%s)_$$"
work=$(mktemp -d "${HOME}/.i8-restore-XXXXXXXX")
trap 'rm -f -- "$work/state.dump"; rmdir -- "$work"' EXIT
db createdb -U i8 "$scratch"
cleanup() {
    db dropdb -U i8 "$scratch"
    rm -f -- "$work/state.dump"
    rmdir -- "$work"
}
trap cleanup EXIT
db pg_dump -U i8 -Fc -n msmonitor_i8 i8 >"$work/state.dump"
db pg_restore -U i8 -d "$scratch" -x --exit-on-error <"$work/state.dump"
# Compare all three tables while the isolated lab has no client traffic.
for table in daemon_sessions commands summary_batches; do
    sql="COPY (SELECT to_jsonb(t)::text FROM msmonitor_i8.$table t ORDER BY 1) TO STDOUT"
    original=$(db psql -XqAt -v ON_ERROR_STOP=1 -U i8 -d i8 -c "$sql" | sha256sum)
    restored=$(db psql -XqAt -v ON_ERROR_STOP=1 -U i8 -d "$scratch" -c "$sql" | sha256sum)
    [[ "$original" == "$restored" ]] || { echo "Restore mismatch: $table" >&2; exit 1; }
    rows=$(db psql -XqAt -v ON_ERROR_STOP=1 -U i8 -d "$scratch" -c "SELECT count(*) FROM msmonitor_i8.$table")
    (( rows > 0 )) || { echo "Seed $table before backup" >&2; exit 1; }
    printf '%s verified (%s rows)\n' "$table" "$rows"
done
echo 'PASS: PostgreSQL sessions, commands and summary batches restored into a separate database'
