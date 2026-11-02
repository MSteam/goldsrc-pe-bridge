#!/usr/bin/env python3
"""Prepare Microsoft ABI headers/import library inputs from the local SDK/DLL."""
from pathlib import Path
import json
import subprocess
ROOT=Path(__file__).resolve().parents[1]
SDK=ROOT.parent/'halflife/utils/vgui/include'
for folder in ('vgui_include','win_include'):(ROOT/'build'/folder).mkdir(parents=True,exist_ok=True)
subprocess.run(['python3',str(ROOT/'tools/inspect_pe.py'),str(ROOT/'refs/vgui.dll'),'vgui'],check=True)
metadata=json.loads((ROOT/'logs/vgui/pe_metadata.json').read_text())
# Steam's VGUI1 adds this method to SurfaceBase's virtual table. Its concrete
# Surface implementation is exported; the 2000 DLL has no such export.
has_mouse_pos='?GetMousePos@Surface@vgui@@UAEXAAH0@Z' in metadata['exports']
for source in SDK.glob('*.h'):
    text=source.read_text(encoding='latin1').replace('__declspec( dllexport )','__declspec( dllimport )')
    text=text.replace('enum  KeyCode;','').replace('enum  MouseCode;','')
    if source.name=='VGUI_SurfaceBase.h' and not has_mouse_pos:
        # This method was added after the 2000 ABI and shifts every drawing slot.
        text='\n'.join(line for line in text.splitlines() if 'virtual void GetMousePos' not in line)+'\n'
    (ROOT/'build/vgui_include'/source.name).write_text(text)
(ROOT/'build/vgui_include/vgui_abi.h').write_text(
    '#pragma once\n#define CB_VGUI_HAS_MOUSEPOS '+str(int(has_mouse_pos))+'\n')
(ROOT/'logs/vgui/abi_profile.json').write_text(json.dumps({
    'sha256':metadata['sha256'],
    'surface_base_mouse_pos':has_mouse_pos,
    'profile':'steam-vgui1' if has_mouse_pos else 'legacy-vgui1',
},indent=2)+'\n')
print('VGUI helper ABI:', 'Steam VGUI1' if has_mouse_pos else 'legacy VGUI1')
(ROOT/'build/vgui.def').write_text('LIBRARY vgui.dll\nEXPORTS\n'+''.join(
    '"'+name+'"'+(' DATA' if export['kind']=='data' else '')+'\n'
    for name,export in metadata['exports'].items()))
(ROOT/'build/win_include/stdlib.h').write_text('typedef unsigned int size_t; extern "C" void exit(int);\n')
(ROOT/'build/win_include/string.h').write_text('extern "C" void *memset(void*,int,unsigned int);\n')
(ROOT/'build/vgui_headers.stamp').touch()
