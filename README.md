# GoldSrc PE bridge

Runs Windows-only Half-Life mods (`client.dll` + server `.dll`) in the native Linux
Steam version of Half-Life. The original DLLs are loaded in-process by a small
PE32 runtime ([taviso/loadlibrary](https://github.com/taviso/loadlibrary) plus the
WinAPI subset these DLLs use); thin Linux libraries adapt the engine interfaces.

- `pe_runtime/` – `libgoldsrc_pe.so`: PE loader, WinAPI/GDI subset, virtual
  `filesystem_stdio.dll`, `fmod.dll` (FMOD 3 via libmpg123 + SDL2) and `SDL2.dll`.
- `client_bridge/` – `client.so` replacing the mod's client library.
- `gunman_bridge/` – `bridge.so` server library; picks the Windows DLL from `liblist.gam`.
- `scout/tools/` – build in the Steam Runtime "scout" SDK, tests, installation.

Tested with Gunman Chronicles (regression fixture), Half-Life: Reprocessing and
Half-Life: Decay. Unsupported WinAPI calls stop with a named error
(`[bridge] UNSUPPORTED WinAPI: …`) instead of silently misbehaving.
Not Wine: single-threaded, i386, only the APIs these mods need.

Build and installation: [BUILD.md](BUILD.md).
