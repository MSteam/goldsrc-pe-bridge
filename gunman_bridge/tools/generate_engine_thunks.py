#!/usr/bin/env python3
"""Generate aligned Linux entries for the current Valve SDK engine interface."""
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]
text=(ROOT.parent/'halflife/engine/eiface.h').read_text()
body=text.split('typedef struct enginefuncs_s',1)[1].split('} enginefuncs_t;',1)[0]
body=re.sub(r'/\*.*?\*/|//[^\n]*','',body,flags=re.S)
names=[match.group(1) for statement in body.split(';')
       if (match:=re.search(r'\(\s*\*\s*(\w+)\s*\)',statement))]
assert len(names)==159 and names[0]=='pfnPrecacheModel'
special={'pfnClientCommand':'aligned_client_command','pfnAlertMessage':'aligned_alert','pfnEngineFprintf':'aligned_engine_fprintf'}
(ROOT/'build/engine_thunks.inc').write_text('// Generated aligned native callback entries.\n'+''.join(
    f'engine_pe.{name} = engine_original.{name} ? {special[name]} : nullptr;\n' if name in special else
    f'engine_pe.{name} = engine_original.{name} ? EngineThunk<decltype(enginefuncs_t::{name}), &enginefuncs_t::{name}>::call : nullptr;\n'
    for name in names))
# Slot names for the server smoke test's named traps.
(ROOT/'build/engine_slot_names.inc').write_text(
    'static const char *const engine_slot_names[]={'+','.join(f'"{n}"' for n in names)+'};\n')
print('Aligned engine entries:',len(names))
