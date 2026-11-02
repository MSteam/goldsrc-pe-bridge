# Building

Requirements: `git`, `python3`, `podman`, LLVM with `clang-cl`, `lld-link` and
`llvm-dlltool` (for the small Windows-side helper DLL and ABI checks). No game or
mod files are needed to build.

```bash
git clone --recursive https://github.com/MSteam/goldsrc-pe-bridge.git
cd goldsrc-pe-bridge
bash client_bridge/tools/bootstrap.sh   # taviso/loadlibrary c40033b + pe_runtime/tools/loadlibrary.patch
podman pull registry.gitlab.steamos.cloud/steamrt/scout/sdk@sha256:16256da93a0746b446e212e7b796d1533fe312f6122a15c36a7e6a4d7648744f
bash scout/tools/build.sh
```

The build runs in the Steam Runtime "scout" SDK container without network.
Output: `scout/work/src/{pe_runtime,client_bridge,server_bridge}/build/`, log: `scout/logs/build.log`.

`bash scout/tools/build.sh --test` also runs the tests (PE fixtures, no mods
needed); with `HL_GAME_DIR=<Half-Life> CB_SMOKE_MODS="<mod> ..."` it additionally
loads those installed mods in a mock engine.

## Installing into a mod

```bash
bash scout/tools/install.sh <Half-Life>/<mod> [--vgui-dll <path to vgui.dll>]
```

Copies `cl_dlls/client.so`, `cl_dlls/vgui_host.dll`, `dlls/bridge.so` and
`dlls/libgoldsrc_pe.so` into the mod (previous files are backed up under
`client_bridge/logs/backups/`). The client also needs Valve's Windows
`cl_dlls/vgui.dll`; if the mod does not ship one, pass `--vgui-dll` with the
`vgui.dll` from a Windows Half-Life installation. Then add to the mod's `liblist.gam`:

```
gamedll_linux "dlls/bridge.so"
```

Run the game from Steam as usual. `HL_GAME_DIR=<Half-Life> scout/tools/run_runtime.sh <mod> [engine args]`
starts it from a terminal in the same runtime; `CB_TEST_FRAMES=N` quits after N level frames.
