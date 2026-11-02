#!/usr/bin/env bash
# Usage: install.sh <mod directory>   e.g. install.sh ../../../reprocessing
set -euo pipefail
scout_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
[[ $# -ge 1 ]] || { echo "usage: $0 <mod directory> [install.py options]" >&2; exit 2; }
python3 "$scout_dir/tools/audit.py"
python3 "$scout_dir/../client_bridge/tools/install.py" \
 --build-root "$scout_dir/work/Half-Life/Rewolf/mod_src" --mod "$@"
