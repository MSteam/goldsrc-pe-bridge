#!/usr/bin/env python3
"""Stage a clean Scout build; host Python generates data, never Linux objects.

Sources live in the repository root (BRIDGE); the game in HL_GAME_DIR. The staged tree keeps the historical
layout work/Half-Life/Rewolf/mod_src: Gunman is the regression fixture, and the
server tests find work/Half-Life/Rewolf/liblist.gam above their build output.
Further mods (CB_SMOKE_MODS, default "reprocessing decay") are staged under
work/Half-Life/smoke/<mod> for the generic smoke tests.
"""
from pathlib import Path
import os,re,shutil,subprocess
ROOT=Path(__file__).resolve().parents[1]
BRIDGE=ROOT.parent
GAME=Path(os.environ.get('HL_GAME_DIR',BRIDGE.parents[1])).resolve()
GUNMAN=GAME/os.environ.get('CB_GUNMAN_MOD','rewolf')
SMOKE_MODS=os.environ.get('CB_SMOKE_MODS','reprocessing decay').split()
WORK=ROOT/'work/Half-Life'; SRC=WORK/'Rewolf/mod_src'
loader_source=BRIDGE/'client_bridge/refs/loadlibrary'
# vgui.dll is Valve's binary and is not distributed with the sources: take Gunman's copy.
vgui=BRIDGE/'client_bridge/refs/vgui.dll'
if not vgui.exists():vgui.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(GUNMAN/'cl_dlls/vgui.dll',vgui)
assert subprocess.check_output(['git','-C',str(loader_source),'rev-parse','HEAD'],text=True).strip()=='c40033b12fd82514f8ee4e42a25906b773b91b5c'
subprocess.run(['git','-C',str(loader_source),'apply','--reverse','--check',str(BRIDGE/'pe_runtime/tools/loadlibrary.patch')],check=True)
# Host clang-cl provides the Microsoft vftable layout the PE code expects.
subprocess.run(['python3',str(BRIDGE/'pe_runtime/tools/check_filesystem_abi.py')],check=True)
subprocess.run(['python3',str(BRIDGE/'client_bridge/tools/check_client_exports_abi.py')],check=True)
if WORK.exists():shutil.rmtree(WORK)
SRC.mkdir(parents=True)
shutil.copy2(BRIDGE/'build_settings.mk',SRC/'build_settings.mk')
for name in ['client_bridge','gunman_bridge','pe_runtime']:
 source=BRIDGE/name;target=SRC/name;target.mkdir()
 shutil.copy2(source/'Makefile',target/'Makefile')
 for part in ['src','tools','tests']:
  if (source/part).exists():shutil.copytree(source/part,target/part)
 for part in ['build','logs']:(target/part).mkdir()
(SRC/'halflife').symlink_to(BRIDGE/'halflife',target_is_directory=True)
loader=SRC/'client_bridge/refs/loadlibrary'
shutil.copytree(loader_source,loader,
 ignore=shutil.ignore_patterns('.git','*.o','*.a'))
shutil.copy2(BRIDGE/'client_bridge/refs/vgui.dll',loader.parent/'vgui.dll')
# This helper is PE32/MSVC ABI, not an ELF library. Its toolchain runs on
# the host; no host libc/libstdc++ can enter the resulting Windows DLL.
subprocess.run(['make','-C',str(BRIDGE/'client_bridge'),'build/vgui_host.dll'],check=True)
shutil.copy2(BRIDGE/'client_bridge/build/vgui_host.dll',SRC/'client_bridge/build/vgui_host.dll')
subprocess.run(['bash',str(BRIDGE/'gunman_bridge/tools/build_export_fixture.sh')],check=True)
shutil.copy2(BRIDGE/'gunman_bridge/build/fixture_variant.dll',SRC/'gunman_bridge/build/fixture_variant.dll')
subprocess.run(['bash',str(BRIDGE/'gunman_bridge/tools/build_winapi_fixture.sh')],check=True)
shutil.copy2(BRIDGE/'gunman_bridge/build/winapi_fixture.dll',SRC/'gunman_bridge/build/winapi_fixture.dll')
for folder in ['dlls','cl_dlls']:(WORK/'Rewolf'/folder).mkdir()
for name in ['gunman.dll']:(WORK/'Rewolf/dlls'/name).symlink_to(GUNMAN/'dlls'/name)
for name in ['client.dll','vgui.dll','vgui_host.dll']:
 (WORK/'Rewolf/cl_dlls'/name).symlink_to(GUNMAN/'cl_dlls'/name)
(WORK/'vgui.so').symlink_to(GAME/'vgui.so')
(WORK/'Rewolf/liblist.gam').symlink_to(GUNMAN/'liblist.gam')
def gamedll(liblist):
 # Same rule as gunman_bridge/src/config.cpp: the first gamedll entry wins.
 text=re.sub(r'/\*.*?\*/|//[^\n]*','',liblist.read_text(encoding='latin1'),flags=re.S)
 match=re.search(r'(?m)^\s*"?gamedll"?\s+"([^"]+)"',text)
 assert match,f'No gamedll in {liblist}'
 return match.group(1).replace('\\','/')
for mod in SMOKE_MODS:
 source=GAME/mod;target=WORK/'smoke'/mod
 server=gamedll(source/'liblist.gam')
 (target/'cl_dlls').mkdir(parents=True);(target/server).parent.mkdir(parents=True,exist_ok=True)
 (target/'liblist.gam').symlink_to(source/'liblist.gam')
 (target/'cl_dlls/client.dll').symlink_to(source/'cl_dlls/client.dll')
 (target/server).symlink_to(source/server)
 (target/'server_dll').write_text(server+'\n')
subprocess.run(['python3',str(SRC/'gunman_bridge/tools/inspect_pe.py')],check=True,stdout=subprocess.DEVNULL)
for name,script in [('gunman_bridge','generate_export_slots.py'),('gunman_bridge','generate_engine_thunks.py'),('client_bridge','generate_thunks.py')]:
 subprocess.run(['python3',str(SRC/name/'tools'/script)],check=True)
print(SRC)
