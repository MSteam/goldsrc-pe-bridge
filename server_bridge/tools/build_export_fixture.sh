#!/usr/bin/env bash
set -euo pipefail
bridge_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$bridge_dir"
mkdir -p build
clang-cl /nologo /c /GS- /Zl /O1 /clang:-fno-builtin /Fo:build/export_fixture.obj \
  --target=i686-pc-windows-msvc tests/export_fixture.c
lld-link /dll /nodefaultlib /machine:x86 /entry:DllMain /base:0x30000000 \
  /def:tests/export_fixture.def /out:build/fixture_variant.dll build/export_fixture.obj
