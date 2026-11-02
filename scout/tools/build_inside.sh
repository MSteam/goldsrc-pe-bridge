#!/usr/bin/env bash
set -euo pipefail
source_dir="$1"
cd "$source_dir"
[[ "$(. /etc/os-release; echo "$ID:$VERSION_CODENAME:$VARIANT")" == steamrt:scout:SDK ]]
cat /etc/os-release
gcc-9 --version | head -1
g++-9 --version | head -1
# Old scout libc headers lack this newer Linux kernel flag. The loader
# already requires a kernel implementing MAP_FIXED_NOREPLACE (Linux >=4.17).
common="$(make -s -f build_settings.mk --eval='print:;@echo $(COMMON)' print) -DMAP_FIXED_NOREPLACE=0x100000"
make -C gunman_bridge -j2 CC=gcc-9 CXX=g++-9 COMMON="$common" all
make -C client_bridge -j2 CC=gcc-9 CXX=g++-9 COMMON="$common" build/client.so build/load build/handshake build/smoke build/mouse_input_test
# Gunman is the regression fixture: these tests must pass.
make -C gunman_bridge CC=gcc-9 CXX=g++-9 COMMON="$common" test
export LD_LIBRARY_PATH="$PWD/../..${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export CB_VGUI_DLL="$PWD/client_bridge/refs/vgui.dll" CB_VGUI_HOST="$PWD/client_bridge/build/vgui_host.dll"
export CB_HEADLESS_VGUI=1
(
 export CB_CLIENT_DLL="$CB_ORIGINAL_MOD/cl_dlls/client.dll" CB_ASSET_ROOT="$CB_ORIGINAL_MOD"
 cd client_bridge
 ./build/mouse_input_test
 for order in server-first client-first; do
  CB_TEST_MOUSE=1 CB_TEST_EMPTY_SPRITES=1 ./build/handshake ./build/client.so init paint "$order" >"logs/scout_${order}.log" 2>&1
  tail -n3 "logs/scout_${order}.log"
 done
)
# Further mods are being brought up: record how far each one gets without
# failing the build. The report lists the first unsupported WinAPI/engine call.
smoke_root="$(cd ../../smoke 2>/dev/null && pwd || true)"
summary=client_bridge/logs/smoke_summary.txt;: >"$summary"
for mod in ${smoke_root:+"$smoke_root"/*}; do
 [[ -d "$mod" ]] || continue
 name="$(basename "$mod")"
 # The real bridge.so selects the DLL from the mod's liblist.gam, as in the game.
 ln -sfn "$PWD/gunman_bridge/build/bridge.so" "$mod/dlls/bridge.so"
 set +e
 (cd client_bridge && CB_CLIENT_DLL="$mod/cl_dlls/client.dll" CB_ASSET_ROOT="$CB_GAME_DIR/$name" \
  CB_FILESYSTEM_SO="$CB_GAME_DIR/filesystem_stdio.so" CB_SMOKE_SERVER_SO="$mod/dlls/bridge.so" timeout 60 ./build/smoke ./build/client.so vid) >"client_bridge/logs/smoke_${name}_client.log" 2>&1
 client=$?
 (cd gunman_bridge && LD_LIBRARY_PATH="$PWD/../pe_runtime/build:$LD_LIBRARY_PATH" CB_ASSET_ROOT="$CB_GAME_DIR/$name" \
  CB_FILESYSTEM_SO="$CB_GAME_DIR/filesystem_stdio.so" timeout 60 ./build/server_smoke "$mod/dlls/bridge.so") >"client_bridge/logs/smoke_${name}_server.log" 2>&1
 server=$?
 set -e
 for side in client server; do
  status=$([[ $side == client ]] && echo $client || echo $server)
  log="client_bridge/logs/smoke_${name}_${side}.log"
  reason="$(grep -m1 -E 'UNSUPPORTED|FAIL|FATAL' "$log" || grep -E 'PASS' "$log" | tail -n1)"
  printf '%s %s exit=%s %s\n' "$name" "$side" "$status" "$reason" | tee -a "$summary"
 done
done
