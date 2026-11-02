#!/usr/bin/env python3
"""Generate typed, aligned callbacks from the checked-in Valve SDK."""
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]
SDK=ROOT.parent/'halflife'
def clean(text): return re.sub(r'/\*.*?\*/|//[^\n]*','',text,flags=re.S)
text=clean((SDK/'engine/APIProxy.h').read_text(encoding='latin1'))
body=text.split('typedef struct cl_enginefuncs_s',1)[1].split('} cl_enginefunc_t;',1)[0]
members=re.findall(r'\b(\w+)\s*;',body)
# The 2000 ABI ends at pNetAPI (87 pointers); HL25 has 135. Older PE clients
# read only their prefix, newer ones must not call native code unaligned.
assert len(members)==135 and members[86]=='pNetAPI' and members[-1]=='pfnFilteredClientCmd'
special={'Con_Printf','Con_DPrintf','Con_NPrintf','Con_NXPrintf','VGui_GetPanel','VGui_ViewportPaintBackground','pfnGetPlayerInfo','pfnSPR_GetList'}
lines=[]
for name in members:
    if name.startswith('p') and name.endswith('API') or name=='pVoiceTweak': continue
    target='aligned_'+name if name in special else f'EngineThunk<decltype(cl_enginefunc_t::{name}), &cl_enginefunc_t::{name}>::call'
    lines.append(f'engine_pe.{name}=engine_native.{name}?{target}:nullptr;\n')
for filename,tag,typ,field in [('event_api.h','event_api_s','event_api_t','pEventAPI'),
                             ('triangleapi.h','triangleapi_s','triangleapi_t','pTriAPI'),
                             ('r_efx.h','efx_api_s','efx_api_t','pEfxAPI'),
                             ('demo_api.h','demo_api_s','demo_api_t','pDemoAPI'),
                             ('net_api.h','net_api_s','net_api_t','pNetAPI')]:
    source=clean((SDK/'common'/filename).read_text(encoding='latin1'))
    section=source.split('struct '+tag,1)[1].split('}',1)[0]
    names=[m.group(1) for part in section.split(';') if (m:=re.search(r'\(\s*\*\s*(\w+)\s*\)',part))]
    lines.append(f'if(engine_native.{field}) {{ table_{field}=*engine_native.{field};\n')
    for name in names:
        lines.append(f'table_{field}.{name}=engine_native.{field}->{name}?TableThunk<decltype({typ}::{name}),&{typ}::{name},&cl_enginefunc_t::{field}>::call:nullptr;\n')
    lines.append(f'engine_pe.{field}=&table_{field}; }}\n')
(ROOT/'build/engine_thunks.inc').write_text(''.join(lines))
# IEngineStudio, handed to the PE by HUD_GetStudioModelInterface.
studio=clean((SDK/'common/r_studioint.h').read_text(encoding='latin1'))
studio=studio.split('typedef struct engine_studio_api_s',1)[1].split('} engine_studio_api_t;',1)[0]
studio_names=[m.group(1) for part in studio.split(';') if (m:=re.search(r'\(\s*\*\s*(\w+)\s*\)',part))]
assert len(studio_names)==46 and studio_names[40]=='IsHardware'
(ROOT/'build/studio_thunks.inc').write_text(''.join(
    f'studio_pe.{n}=studio_native.{n}?StudioThunk<decltype(engine_studio_api_t::{n}),&engine_studio_api_t::{n}>::call:nullptr;\n'
    for n in studio_names))
# Slot names, for diagnostics that name an engine callback.
tables=[]
for filename,tag,field in [('event_api.h','event_api_s','pEventAPI'),('triangleapi.h','triangleapi_s','pTriAPI'),
                           ('r_efx.h','efx_api_s','pEfxAPI'),('demo_api.h','demo_api_s','pDemoAPI'),('net_api.h','net_api_s','pNetAPI')]:
    source=clean((SDK/'common'/filename).read_text(encoding='latin1'))
    section=source.split('struct '+tag,1)[1].split('}',1)[0]
    names=[m.group(1) for part in section.split(';') if (m:=re.search(r'\(\s*\*\s*(\w+)\s*\)',part))]
    tables.append((field,names))
(ROOT/'build/engine_slot_names.inc').write_text(
    'static const char *const engine_slot_names[]={'+','.join(f'"{n}"' for n in members)+'};\n'+
    ''.join(f'static const char *const {field}_slot_names[]={{'+','.join(f'"{n}"' for n in names)+'};\n' for field,names in tables))
with open(ROOT/'build/engine_slot_names.inc','a') as names:
    names.write('static const char *const studio_slot_names[]={'+','.join(f'"{n}"' for n in studio_names)+'};\n')
print('Client engine table:',len(members),'pointers')
