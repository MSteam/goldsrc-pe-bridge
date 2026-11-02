#!/usr/bin/env python3
"""Inspect PE32 headers and export RVAs without executing the DLL."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DLL = ROOT.parents[1] / 'dlls/gunman.dll'
data = DLL.read_bytes()
def u16(offset): return struct.unpack_from('<H', data, offset)[0]
def u32(offset): return struct.unpack_from('<I', data, offset)[0]
nt = u32(0x3c)
assert data[:2] == b'MZ' and data[nt:nt+4] == b'PE\0\0'
optional = nt + 24
assert u16(optional) == 0x10b and u16(nt+4) == 0x14c
base = u32(optional+28)
sections = []
for index in range(u16(nt+6)):
    at = optional + u16(nt+20) + index*40
    sections.append(dict(name=data[at:at+8].split(b'\0')[0].decode(),
        size=u32(at+8), rva=u32(at+12), raw_size=u32(at+16),
        offset=u32(at+20), flags=u32(at+36)))
def raw(rva):
    if rva < u32(optional+60): return rva
    for section in sections:
        if section['rva'] <= rva < section['rva'] + section['raw_size']:
            return section['offset'] + rva-section['rva']
    raise ValueError(f'Unmapped RVA {rva:#x}')
def string(rva):
    at=raw(rva)
    return data[at:data.index(b'\0',at)].decode('ascii')
directory=raw(u32(optional+96))
names=u32(directory+24)
functions=u32(directory+28)
name_table=u32(directory+32)
ordinal_table=u32(directory+36)
exports={}
for index in range(names):
    name=string(u32(raw(name_table)+index*4))
    ordinal=u16(raw(ordinal_table)+index*2)
    rva=u32(raw(functions)+ordinal*4)
    section=next(s for s in sections if s['rva'] <= rva < s['rva']+max(s['size'],s['raw_size']))
    exports[name]=dict(rva=rva, address=base+rva, kind='code' if section['flags'] & 0x20000000 else 'data')
report=dict(path=str(DLL),sha256=hashlib.sha256(data).hexdigest(),image_base=base,
    image_size=u32(optional+56),entry_rva=u32(optional+16),sections=sections,exports=exports)
(ROOT/'logs/pe_metadata.json').write_text(json.dumps(report,indent=2)+'\n')
print('image',hex(base),'size',hex(report['image_size']),'entry',hex(report['entry_rva']))
print('exports',len(exports),'code',sum(e['kind']=='code' for e in exports.values()))
blocks=[]
for name in ['GiveFnptrsToDll','GetEntityAPI','GetEntityAPI2']:
    at=exports[name]['address']
    print(name,hex(at))
    blocks.append(name+'\n'+subprocess.check_output(['objdump','-d','-Mintel',
        f'--start-address={at}',f'--stop-address={at+120}',str(DLL)],text=True))
at=base+report['entry_rva']
blocks.append('DLL entry\n'+subprocess.check_output(['objdump','-d','-Mintel',
    f'--start-address={at}',f'--stop-address={at+160}',str(DLL)],text=True))
(ROOT/'logs/api_disassembly.txt').write_text('\n'.join(blocks))
