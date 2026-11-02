#!/usr/bin/env python3
"""Install the local experimental bridge; back up existing destination files."""
from pathlib import Path
from datetime import datetime
import hashlib
import json
import shutil
import argparse
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--mod',type=Path,required=True,help='mod directory to install into, e.g. Half-Life/reprocessing')
parser.add_argument('--build-root',type=Path,help='mod_src directory containing separately built client/server/runtime artifacts')
parser.add_argument('--client-only',action='store_true',help='Only replace cl_dlls/client.so')
args=parser.parse_args()
MOD=args.mod.resolve()
if not (MOD/'liblist.gam').is_file():parser.error(f'{MOD} has no liblist.gam')
SOURCE=args.build_root.resolve() if args.build_root else ROOT.parent
BACKUP=ROOT/'logs/backups'/(MOD.name+'_'+datetime.now().strftime('install_%Y%m%d_%H%M%S'))
BACKUP.mkdir(parents=True,exist_ok=True)
items=[(SOURCE/'pe_runtime/build/libgoldsrc_pe.so',MOD/'dlls/libgoldsrc_pe.so'),
       (SOURCE/'client_bridge/build/client.so',MOD/'cl_dlls/client.so'),
       (SOURCE/'client_bridge/build/vgui_host.dll',MOD/'cl_dlls/vgui_host.dll'),
       (SOURCE/'client_bridge/refs/vgui.dll',MOD/'cl_dlls/vgui.dll'),
       (SOURCE/'gunman_bridge/build/bridge.so',MOD/'dlls/bridge.so')]
# gunman.so is the historical name of bridge.so; refresh it only where installed.
if (MOD/'dlls/gunman.so').exists():items.append((SOURCE/'gunman_bridge/build/gunman.so',MOD/'dlls/gunman.so'))
if args.client_only:items=[item for item in items if item[1]==MOD/'cl_dlls/client.so']
report={'mod':str(MOD),'backup':str(BACKUP),'build_root':str(SOURCE),'scope':'client-only' if args.client_only else 'all','files':{}}
provenance=SOURCE/'build_environment.json'
if provenance.exists():report['build_environment']=json.loads(provenance.read_text())
for source,dest in items:
    data=source.read_bytes()
    if dest.exists():
        target=BACKUP/dest.relative_to(MOD);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(dest,target)
    temporary=dest.with_name(dest.name+'.installing');shutil.copy2(source,temporary);temporary.replace(dest)
    report['files'][str(dest)]={'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)}
retired=MOD/'dlls/librewolf_pe.so'
if not args.client_only and retired.exists():
    target=BACKUP/retired.relative_to(MOD);target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(retired,target)
    retired.unlink()
    report['retired_files']=[str(retired)]
(ROOT/'logs'/f'install_manifest_{MOD.name}.json').write_text(json.dumps(report,indent=2)+'\n')
print('Installed bridge files; previous files:',BACKUP)
