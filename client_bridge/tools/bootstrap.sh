#!/usr/bin/env bash
set -euo pipefail
bridge_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
loader_dir="$bridge_dir/refs/loadlibrary"
revision=c40033b12fd82514f8ee4e42a25906b773b91b5c
patch="$bridge_dir/../pe_runtime/tools/loadlibrary.patch"
mkdir -p "$bridge_dir"/{build,logs,refs,run} "$bridge_dir/../pe_runtime/build" "$bridge_dir/../gunman_bridge/build"
if [[ ! -d "$loader_dir/.git" ]]; then
    git clone --no-checkout https://github.com/taviso/loadlibrary.git "$loader_dir"
    git -C "$loader_dir" checkout --detach "$revision"
fi
if [[ "$(git -C "$loader_dir" rev-parse HEAD)" != "$revision" ]]; then
    echo 'Unexpected loader revision; inspect refs/loadlibrary.' >&2;exit 1
fi
if git -C "$loader_dir" apply --reverse --check "$patch" 2>/dev/null; then
    :
elif git -C "$loader_dir" diff --quiet && git -C "$loader_dir" apply --check "$patch"; then
    git -C "$loader_dir" apply "$patch"
else
    echo 'Unexpected loader changes; preserve them and inspect the patch.' >&2;exit 1
fi
# --fetch-only: loader checkout and patch for scout/tools/build.sh, no host build.
[[ "${1:-}" == --fetch-only ]] && exit 0
if [[ ! -f "$bridge_dir/refs/vgui.dll" ]]; then
    echo 'Place the Windows vgui.dll (e.g. Gunman cl_dlls/vgui.dll) in refs/vgui.dll.' >&2;exit 1
fi
make -C "$bridge_dir" -j"${CB_BUILD_JOBS:-2}" all
make -C "$bridge_dir/../gunman_bridge" -j"${CB_BUILD_JOBS:-2}" all
