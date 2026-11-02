#!/usr/bin/env python3
"""Verify both IFileSystem vtable orders used by src/filesystem.c.

Native (Itanium) order is the declaration order in halflife/public/FileSystem.h.
Microsoft order is taken from clang-cl's MSVC vftable layout of the same header.
"""
from pathlib import Path
import re,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[1]
SDK=ROOT.parent/'halflife'
HEADER=SDK/'public/FileSystem.h'
SOURCE=(ROOT/'src/filesystem.c').read_text()
def key(name,params):
    if name=='Size':return 'SizeHandle' if 'FileHandle_t' in params else 'SizeName'
    return name
body=HEADER.read_text().split('class IFileSystem : public IBaseInterface',1)[1]
body=re.sub(r'//[^\n]*|/\*.*?\*/','',body,flags=re.S)
declared=[key(m.group(1),m.group(2)) for statement in body.split(';')
          if (m:=re.search(r'virtual[^(]*?\b(\w+)\s*\((.*)\)\s*=\s*0\s*$',statement.strip(),re.S))]
enum=re.search(r'enum native_slot \{(.*?)\};',SOURCE,re.S).group(1)
native=[n[3:] for n in re.findall(r'FS_\w+',enum) if n!='FS_METHOD_COUNT']
assert native==declared,('native order differs from FileSystem.h',native,declared)
with tempfile.TemporaryDirectory() as work:
    probe=Path(work)/'probe.cpp'
    lines=['#include "FileSystem.h"','struct Probe : IFileSystem {']
    for m in re.finditer(r'virtual\s+([^;]*?)\s*=\s*0\s*;',body,re.S):
        lines.append('  '+re.sub(r'=\s*0L','',m.group(1))+' override;')
    lines+=['};','void touch(Probe *p){p->Mount();}']
    probe.write_text('\n'.join(lines))
    # archtypes.h pulls in MSVC STL headers that clang-cl lacks here; the
    # interface needs none of it.
    dump=subprocess.run(['clang-cl','/nologo','/c','--target=i686-pc-windows-msvc','/DARCHTYPES_H',
        f'/I{SDK/"public"}',f'/I{SDK/"common"}','-Xclang','-fdump-vtable-layouts',f'/Fo:{Path(work)/"probe.obj"}',str(probe)],
        capture_output=True,text=True)
    if dump.returncode:sys.exit(dump.stdout+dump.stderr)
table=dump.stdout.split("VFTable indices for 'IFileSystem'",1)[1].split('\n\n',1)[0]
msvc=[]
for line in table.splitlines()[1:]:
    m=re.search(r'IFileSystem::(~?\w+)\(([^)]*)\)',line)
    if m:msvc.append('Destructor' if m.group(1).startswith('~') else key(m.group(1),m.group(2)))
initializer=re.search(r'msvc_vtable\[\]=\{(.*?)\};',SOURCE,re.S).group(1)
ours=[n[3:] for n in re.findall(r'fs_\w+',initializer)]
assert ours==msvc,('MSVC vftable differs from clang-cl',ours,msvc)
print(f'PASS: IFileSystem ABI: {len(native)} native slots (+2 destructors), {len(msvc)} MSVC slots match')
