# Building

Requirements (host): `git`, `python3`, `podman`, LLVM with `clang-cl`, `lld-link`,
`llvm-dlltool` (Windows-side helper DLLs and ABI checks); Steam Half-Life (Linux)
with Gunman Chronicles installed (`rewolf`, used by the mandatory regression tests).

```bash
git clone --recursive https://github.com/MSteam/goldsrc-pe-bridge.git
cd goldsrc-pe-bridge
bash client_bridge/tools/bootstrap.sh   # fetches loadlibrary c40033b, applies pe_runtime/tools/loadlibrary.patch
podman pull registry.gitlab.steamos.cloud/steamrt/scout/sdk@sha256:16256da93a0746b446e212e7b796d1533fe312f6122a15c36a7e6a4d7648744f
export HL_GAME_DIR="$HOME/.local/share/Steam/steamapps/common/Half-Life"
bash scout/tools/build.sh
```

`bootstrap.sh` ends with a host build; only its first steps (loader checkout and
patch) are needed, the release build is `scout/tools/build.sh`. It builds in the
Scout SDK container without network, runs the Gunman tests (must pass) and smoke
tests of the mods in `CB_SMOKE_MODS` (default `reprocessing decay`, informational).
Logs: `scout/logs/`, output: `scout/work/Half-Life/Rewolf/mod_src/*/build/`.

## Installing into a mod

```bash
bash scout/tools/install.sh "$HL_GAME_DIR/<mod>"
```

This copies `cl_dlls/client.so`, `cl_dlls/vgui_host.dll`, `cl_dlls/vgui.dll` (Gunman's),
`dlls/bridge.so` and `dlls/libgoldsrc_pe.so` (previous files are backed up under
`client_bridge/logs/backups/`). Then add to the mod's `liblist.gam`:

```
gamedll_linux "dlls/bridge.so"
```

Run the game from Steam as usual. `scout/tools/run_runtime.sh <mod> [engine args]` starts it
from a terminal in the same runtime; `CB_TEST_FRAMES=N` quits after N level frames.
