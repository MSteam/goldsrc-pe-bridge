#!/usr/bin/env bash
# Use the same scout-on-soldier environment as Steam, with the original hl.sh.
# Usage: run_runtime.sh <mod directory name> [engine arguments...]
set -euo pipefail
scout_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
game_dir="$(cd -- "${HL_GAME_DIR:-$scout_dir/../../..}" && pwd)"
[[ $# -ge 1 ]] || { echo "usage: $0 <mod> [engine arguments]" >&2; exit 2; }
mod="$1";shift
[[ -f "$game_dir/$mod/liblist.gam" ]] || { echo "no mod $game_dir/$mod" >&2; exit 2; }
soldier="${SOLDIER_RUNTIME:-$(cd -- "$game_dir/../SteamLinuxRuntime_soldier" && pwd)}"
scout="${SCOUT_RUNTIME:-$HOME/.local/share/Steam/steamapps/common/SteamLinuxRuntime}"
export STEAM_COMPAT_MOUNTS="$scout:$game_dir${STEAM_COMPAT_MOUNTS:+:$STEAM_COMPAT_MOUNTS}"
export SteamAppId=70
cd "$game_dir"
exec "$soldier/run" -- "$scout/scout-on-soldier-entry-point-v2" -- \
 ./hl.sh -steam -game "$mod" "$@"
