#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
bridge_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$bridge_dir"
fixture="run/winapi_fixture/mod"
mkdir -p "$fixture/dlls"
cp build/bridge.so "$fixture/dlls/winapi_bridge.so"
cp ../pe_runtime/build/libgoldsrc_pe.so "$fixture/dlls/libgoldsrc_pe.so"
cp build/winapi_fixture.dll "$fixture/dlls/winapi_fixture.dll"
printf 'game "WinAPI fixture"\ngamedll "dlls\\winapi_fixture.dll"\ngamedll_linux "dlls/winapi_bridge.so"\n' >"$fixture/liblist.gam"
# Any stock MP3 serves for the virtual fmod.dll; SDL's dummy driver mixes in real time without a sound card.
song="${CB_GAME_DIR:-/nonexistent}/valve/media/Half-Life01.mp3"
[[ -f "$song" ]] && export CB_TEST_MP3="$song" SDL_AUDIODRIVER=dummy
env -u HLBRIDGE_DLL ./build/winapi_probe "$PWD/$fixture/dlls/winapi_bridge.so" "$PWD/$fixture/liblist.gam" "$PWD/$fixture/dlls"
