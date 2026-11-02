#!/usr/bin/env bash
# Build in the Steam Runtime scout SDK: build.sh [--test]
#   --test  also run the mod-free tests and smoke tests of CB_SMOKE_MODS
#           (mods installed in HL_GAME_DIR, e.g. CB_SMOKE_MODS="decay reprocessing").
set -euo pipefail
scout_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
bridge_dir="$(cd -- "$scout_dir/.." && pwd)"
tests="${1:-}"
[[ -z "$tests" || "$tests" == --test ]] || { echo "usage: $0 [--test]" >&2; exit 2; }
# Pin the official image; override explicitly to upgrade.
image="${SCOUT_SDK_IMAGE:-registry.gitlab.steamos.cloud/steamrt/scout/sdk@sha256:16256da93a0746b446e212e7b796d1533fe312f6122a15c36a7e6a4d7648744f}"
mkdir -p "$scout_dir/logs"
podman image inspect "$image" >"$scout_dir/logs/sdk_image.json"
python3 "$scout_dir/tools/prepare.py" $tests >"$scout_dir/logs/prepare.log" 2>&1
source_dir="$scout_dir/work/src"
mounts=(-v "$bridge_dir:$bridge_dir:ro" -v "$scout_dir:$scout_dir:rw")
game_dir=""
if [[ -n "$tests" && -n "${HL_GAME_DIR:-}" ]]; then
 game_dir="$(cd -- "$HL_GAME_DIR" && pwd)";mounts=(-v "$game_dir:$game_dir:ro" "${mounts[@]}")
fi
podman run --rm --network=none --userns=keep-id "${mounts[@]}" -e CB_GAME_DIR="$game_dir" "$image" \
 bash "$scout_dir/tools/build_inside.sh" "$source_dir" $tests >"$scout_dir/logs/build.log" 2>&1
python3 "$scout_dir/tools/audit.py" >>"$scout_dir/logs/build.log"
python3 "$scout_dir/tools/provenance.py"
printf 'Scout build completed: %s\n' "$source_dir"
if [[ -n "$tests" ]]; then
 grep -E '^(PASS|FAIL)' "$scout_dir/logs/build.log" || true
 if [[ -s "$source_dir/client_bridge/logs/smoke_summary.txt" ]]; then
  printf 'Smoke tests:\n';cat "$source_dir/client_bridge/logs/smoke_summary.txt"
 fi
fi
