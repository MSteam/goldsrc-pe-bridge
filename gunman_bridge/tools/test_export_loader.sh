#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
bridge_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$bridge_dir"
fixture="run/export_fixture/mod with spaces"
mkdir -p "$fixture/dlls"
cp build/bridge.so "$fixture/dlls/renamed_bridge.so"
cp ../pe_runtime/build/libgoldsrc_pe.so "$fixture/dlls/libgoldsrc_pe.so"
cp build/fixture_variant.dll "$fixture/dlls/fixture_variant.dll"
# Comments must not select the wrong DLL; backslashes must survive tokenization.
printf '// gamedll "missing.dll"\r\n/* gamedll "also_missing.dll" */\r\ngame "Fixture"\r\ngamedll "dlls\\fixture_variant.dll"\r\ngamedll_linux "dlls/renamed_bridge.so"\r\n' >"$fixture/liblist.gam"
env -u HLBRIDGE_DLL -u GUNMAN_BRIDGE_DLL ./build/export_loader "$fixture/dlls/renamed_bridge.so"
printf 'game "Missing declaration"\n' >"$fixture/liblist.gam"
if env -u HLBRIDGE_DLL -u GUNMAN_BRIDGE_DLL ./build/export_loader "$fixture/dlls/renamed_bridge.so" >logs/missing_gamedll.log 2>&1; then
  echo 'FAIL: missing gamedll was accepted' >&2;exit 1
fi
grep -q 'No gamedll entry' logs/missing_gamedll.log
printf 'gamedll "dlls\\does_not_exist.dll"\n' >"$fixture/liblist.gam"
if env -u HLBRIDGE_DLL -u GUNMAN_BRIDGE_DLL ./build/export_loader "$fixture/dlls/renamed_bridge.so" >logs/missing_dll.log 2>&1; then
  echo 'FAIL: missing Windows DLL was accepted' >&2;exit 1
fi
grep -q 'Cannot resolve Windows DLL' logs/missing_dll.log
printf 'PASS: missing configuration and missing DLL diagnosed\n'
