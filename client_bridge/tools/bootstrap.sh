#!/usr/bin/env bash
# Fetch taviso/loadlibrary at the pinned revision and apply the bridge patch.
set -euo pipefail
bridge_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
loader_dir="$bridge_dir/refs/loadlibrary"
revision=c40033b12fd82514f8ee4e42a25906b773b91b5c
patch="$bridge_dir/../pe_runtime/tools/loadlibrary.patch"
mkdir -p "$bridge_dir/refs"
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
echo "loadlibrary $revision with the bridge patch: $loader_dir"
