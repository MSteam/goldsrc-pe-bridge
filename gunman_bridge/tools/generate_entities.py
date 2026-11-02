#!/usr/bin/env python3
"""Legacy Gunman export inventory; the current build uses generate_export_slots.py."""
from pathlib import Path
import json
import re
ROOT=Path(__file__).resolve().parents[1]
metadata=json.loads((ROOT/'logs/pe_metadata.json').read_text())
names=[name for name,entry in metadata['exports'].items()
       if entry['kind']=='code' and re.fullmatch(r'[a-z][A-Za-z0-9_]*',name)]
assert 'worldspawn' in names and 'player' in names
(ROOT/'build/entity_exports.inc').write_text(
    '// Generated from original PE code exports.\n'+''.join(
    f'EXPORT void {name}(entvars_t *vars) {{ forward_entity("{name}", vars); }}\n' for name in names))
(ROOT/'logs/entity_exports.json').write_text(json.dumps(names,indent=2)+'\n')
print('Generated entity forwarders:',len(names))
