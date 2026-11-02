#!/usr/bin/env python3
"""Stage a clean Scout build; host Python generates data, never Linux objects.

Sources live in the repository root (BRIDGE); the staged tree is work/src.
No game or mod files are needed. With --test, the mods named in CB_SMOKE_MODS
are linked from HL_GAME_DIR under work/smoke/<mod> for the smoke tests.
"""
from pathlib import Path
import os,re,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
BRIDGE=ROOT.parent
TESTS='--test' in sys.argv[1:]
WORK=ROOT/'work'; SRC=WORK/'src'
loader_source=BRIDGE/'client_bridge/refs/loadlibrary'
assert subprocess.check_output(['git','-C',str(loader_source),'rev-parse','HEAD'],text=True).strip()=='c40033b12fd82514f8ee4e42a25906b773b91b5c'
subprocess.run(['git','-C',str(loader_source),'apply','--reverse','--check',str(BRIDGE/'pe_runtime/tools/loadlibrary.patch')],check=True)
# Host clang-cl provides the Microsoft vftable layout the PE code expects.
subprocess.run(['python3',str(BRIDGE/'pe_runtime/tools/check_filesystem_abi.py')],check=True)
subprocess.run(['python3',str(BRIDGE/'client_bridge/tools/check_client_exports_abi.py')],check=True)
if WORK.exists():shutil.rmtree(WORK)
SRC.mkdir(parents=True)
shutil.copy2(BRIDGE/'build_settings.mk',SRC/'build_settings.mk')
for name in ['client_bridge','server_bridge','pe_runtime']:
 source=BRIDGE/name;target=SRC/name;target.mkdir()
 shutil.copy2(source/'Makefile',target/'Makefile')
 for part in ['src','tools','tests']:
  if (source/part).exists():shutil.copytree(source/part,target/part)
 for part in ['build','logs']:(target/part).mkdir()
(SRC/'halflife').symlink_to(BRIDGE/'halflife',target_is_directory=True)
shutil.copytree(loader_source,SRC/'client_bridge/refs/loadlibrary',ignore=shutil.ignore_patterns('.git','*.o','*.a'))
# Windows-side helpers are PE32/MSVC ABI, built with the host clang-cl/lld-link;
# no host libc/libstdc++ can enter them.
subprocess.run(['make','-C',str(BRIDGE/'client_bridge'),'build/vgui_host.dll'],check=True)
shutil.copy2(BRIDGE/'client_bridge/build/vgui_host.dll',SRC/'client_bridge/build/vgui_host.dll')
if TESTS:
 for script,dll in [('build_export_fixture.sh','fixture_variant.dll'),('build_winapi_fixture.sh','winapi_fixture.dll')]:
  subprocess.run(['bash',str(BRIDGE/'server_bridge/tools'/script)],check=True)
  shutil.copy2(BRIDGE/'server_bridge/build'/dll,SRC/'server_bridge/build'/dll)
 GAME=Path(os.environ.get('HL_GAME_DIR','')).resolve() if os.environ.get('HL_GAME_DIR') else None
 def gamedll(liblist):
  # Same rule as server_bridge/src/config.cpp: the first gamedll entry wins.
  text=re.sub(r'/\*.*?\*/|//[^\n]*','',liblist.read_text(encoding='latin1'),flags=re.S)
  match=re.search(r'(?m)^\s*"?gamedll"?\s+"([^"]+)"',text)
  assert match,f'No gamedll in {liblist}'
  return match.group(1).replace('\\','/')
 for mod in os.environ.get('CB_SMOKE_MODS','').split():
  source=GAME/mod if GAME else None
  if not source or not (source/'liblist.gam').exists():print(f'smoke: {mod} is not installed, skipped');continue
  target=WORK/'smoke'/mod;server=gamedll(source/'liblist.gam')
  (target/'cl_dlls').mkdir(parents=True);(target/server).parent.mkdir(parents=True,exist_ok=True)
  (target/'liblist.gam').symlink_to(source/'liblist.gam')
  (target/'cl_dlls/client.dll').symlink_to(source/'cl_dlls/client.dll')
  (target/server).symlink_to(source/server)
for name,script in [('server_bridge','generate_export_slots.py'),('server_bridge','generate_engine_thunks.py'),('client_bridge','generate_thunks.py')]:
 subprocess.run(['python3',str(SRC/name/'tools'/script)],check=True)
print(SRC)
