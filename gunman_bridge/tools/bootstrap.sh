#!/usr/bin/env bash
set -euo pipefail
bridge_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
bash "$bridge_dir/../client_bridge/tools/bootstrap.sh"
make -C "$bridge_dir" build/native_host
