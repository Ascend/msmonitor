#!/usr/bin/env bash
set -euo pipefail

center_dir=$(cd "$(dirname "$0")" && pwd)
image=${1:-msmonitor-center:beta}
base=${MSMONITOR_CENTER_BASE_IMAGE:-msmonitor-center:base}

docker build --target center \
  --build-arg "DEBIAN_MIRROR_URL=${DEBIAN_MIRROR_URL:-https://deb.debian.org}" \
  --build-arg "PIP_INDEX_URL=${PIP_INDEX_URL:-https://pypi.org/simple}" \
  --build-arg "PIP_FALLBACK_INDEX_URL=${PIP_FALLBACK_INDEX_URL:-}" \
  -t "$base" "$center_dir/control"
docker build --build-arg "CENTER_IMAGE=$base" -t "$image" "$center_dir"
