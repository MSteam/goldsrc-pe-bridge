#!/usr/bin/env bash
set -euo pipefail
scout_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
bridge_dir="$(cd -- "$scout_dir/.." && pwd)"
game_dir="$(cd -- "${HL_GAME_DIR:-$bridge_dir/../..}" && pwd)"
export HL_GAME_DIR="$game_dir"
gunman_dir="$game_dir/${CB_GUNMAN_MOD:-rewolf}"
# Pin the already available official image; override explicitly to upgrade.
image="${SCOUT_SDK_IMAGE:-registry.gitlab.steamos.cloud/steamrt/scout/sdk@sha256:16256da93a0746b446e212e7b796d1533fe312f6122a15c36a7e6a4d7648744f}"
mkdir -p "$scout_dir/logs"
podman image inspect "$image" >"$scout_dir/logs/sdk_image.json"
python3 "$scout_dir/tools/prepare.py" >"$scout_dir/logs/prepare.log" 2>&1
source_dir="$scout_dir/work/Half-Life/Rewolf/mod_src"
podman run --rm --network=none --userns=keep-id \
 -v "$game_dir:$game_dir:ro" -v "$bridge_dir:$bridge_dir:ro" -v "$scout_dir:$scout_dir:rw" \
 -e CB_ORIGINAL_MOD="$gunman_dir" -e CB_GAME_DIR="$game_dir" "$image" \
 bash "$scout_dir/tools/build_inside.sh" "$source_dir" >"$scout_dir/logs/build.log" 2>&1
python3 "$scout_dir/tools/audit.py" >>"$scout_dir/logs/build.log"
python3 "$scout_dir/tools/provenance.py"
printf 'Scout build and tests completed: %s\n' "$source_dir"
if [[ -s "$source_dir/client_bridge/logs/smoke_summary.txt" ]]; then
 printf 'Smoke tests (informational):\n';cat "$source_dir/client_bridge/logs/smoke_summary.txt"
fi
