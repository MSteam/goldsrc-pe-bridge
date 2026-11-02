#!/usr/bin/env bash
# Host build of a PE32 test DLL that imports KERNEL32 (no CRT).
set -euo pipefail
bridge_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$bridge_dir"
mkdir -p build
llvm-dlltool -m i386 -k -d tests/kernel32_fixture.def -l build/kernel32_fixture.lib
clang-cl /nologo /c /GS- /Zl /O1 /clang:-fno-builtin /Fo:build/winapi_fixture.obj \
  --target=i686-pc-windows-msvc tests/winapi_fixture.c
lld-link /dll /nodefaultlib /machine:x86 /entry:DllMain /base:0x30000000 \
  /def:tests/winapi_fixture.def /out:build/winapi_fixture.dll build/winapi_fixture.obj build/kernel32_fixture.lib
