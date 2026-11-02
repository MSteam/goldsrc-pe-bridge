# Building

The Linux libraries are built in the Steam Runtime "scout" SDK (32-bit, the
runtime Steam runs Half-Life in). Generated headers and the small Windows helper
`vgui_host.dll` are made on the host: `python3` and LLVM (`clang-cl`, `lld-link`,
`llvm-dlltool`).
No game or mod files are needed.

```bash
git clone --recursive https://github.com/MSteam/goldsrc-pe-bridge.git
cd goldsrc-pe-bridge

# PE loader: taviso/loadlibrary at the pinned revision plus the bridge patch
git clone https://github.com/taviso/loadlibrary.git client_bridge/refs/loadlibrary
git -C client_bridge/refs/loadlibrary checkout c40033b12fd82514f8ee4e42a25906b773b91b5c
git -C client_bridge/refs/loadlibrary apply "$PWD/pe_runtime/tools/loadlibrary.patch"

# Host: generated headers (Python 3.6+) and the Windows helper
make -C server_bridge build/engine_thunks.inc build/export_slots.S
make -C client_bridge build/engine_thunks.inc build/vgui_host.dll

# Linux libraries (scout SDK)
podman run --rm --userns=keep-id -v "$PWD:$PWD" -w "$PWD" \
  registry.gitlab.steamos.cloud/steamrt/scout/sdk@sha256:16256da93a0746b446e212e7b796d1533fe312f6122a15c36a7e6a4d7648744f \
  sh -c 'make -C server_bridge CC=gcc-9 CXX=g++-9 && make -C client_bridge CC=gcc-9 CXX=g++-9 build/client.so'
```

Result:

| File | Install to |
|---|---|
| `client_bridge/build/client.so` | `<mod>/cl_dlls/` |
| `client_bridge/build/vgui_host.dll` | `<mod>/cl_dlls/` |
| `server_bridge/build/bridge.so` | `<mod>/dlls/` |
| `pe_runtime/build/libgoldsrc_pe.so` | `<mod>/dlls/` |

The client also needs Valve's Windows `vgui.dll` in `<mod>/cl_dlls/` (from a
Windows Half-Life installation) if the mod does not ship one. Then add to the
mod's `liblist.gam` and start the game from Steam as usual:

```
gamedll_linux "dlls/bridge.so"
```
