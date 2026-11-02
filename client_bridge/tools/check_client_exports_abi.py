#!/usr/bin/env python3
"""Verify the Microsoft vftable slots that src/bridge.cpp uses for the PE's
IGameClientExports against clang-cl's MSVC layout of the SDK header."""
from pathlib import Path
import re,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[1]
SDK=ROOT.parent/'halflife'
source=(ROOT/'src/bridge.cpp').read_text()
ours={m.group(1):int(m.group(2)) for m in re.finditer(r'msvc_slot_(\w+)=(\d+)',source)}
with tempfile.TemporaryDirectory() as work:
    probe=Path(work)/'probe.cpp'
    probe.write_text('#include "cl_dll/IGameClientExports.h"\n'
        'struct Probe : IGameClientExports {const char *GetServerHostName() override;'
        'bool IsPlayerGameVoiceMuted(int) override;void MutePlayerGameVoice(int) override;'
        'void UnmutePlayerGameVoice(int) override;};\nvoid touch(Probe *p){p->GetServerHostName();}\n')
    dump=subprocess.run(['clang-cl','/nologo','/c','--target=i686-pc-windows-msvc',f'/I{SDK/"public"}',
        '-Xclang','-fdump-vtable-layouts',f'/Fo:{Path(work)/"probe.obj"}',str(probe)],capture_output=True,text=True)
    if dump.returncode:sys.exit(dump.stdout+dump.stderr)
table=dump.stdout.split("VFTable indices for 'IGameClientExports'",1)[1].split('\n\n',1)[0]
msvc={}
for line in table.splitlines()[1:]:
    m=re.match(r'\s*(\d+) \| .*IGameClientExports::(~?\w+)\(',line)
    if m:msvc['Destructor' if m.group(2).startswith('~') else m.group(2)]=int(m.group(1))
expected={name:slot for name,slot in msvc.items() if name!='Destructor'}
assert ours==expected,('bridge.cpp slots differ from clang-cl',ours,msvc)
print(f'PASS: IGameClientExports ABI: {len(ours)} MSVC slots match')
