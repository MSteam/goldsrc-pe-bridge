#!/usr/bin/env python3
"""Reject ELF artifacts that exceed Scout's libc ABI or leak build-machine paths."""
from pathlib import Path
import subprocess,json,re,hashlib
ROOT=Path(__file__).resolve().parents[1]; SRC=ROOT/'work/src'
report={}
for relative in ['pe_runtime/build/libgoldsrc_pe.so','client_bridge/build/client.so','server_bridge/build/bridge.so']:
 p=SRC/relative
 header=subprocess.check_output(['readelf','-h',str(p)],text=True)
 assert 'ELF32' in header and 'Intel 80386' in header,p
 dyn=subprocess.check_output(['readelf','-W','-d',str(p)],text=True)
 ver=subprocess.check_output(['readelf','-W','--version-info',str(p)],text=True)
 needed=re.findall(r'Shared library: \[(.*?)\]',dyn)
 if p.name in {'client.so','bridge.so'}:assert 'libgoldsrc_pe.so' in needed,(p,needed)
 if p.name=='client.so':assert 'vgui.so' in needed,(p,needed) # engine VGUI1, linked through a stub
 if p.name=='libgoldsrc_pe.so':assert 'Library soname: [libgoldsrc_pe.so]' in dyn,p
 assert 'libstdc++.so.6' not in needed and 'libgcc_s.so.1' not in needed,(p,needed)
 versions=sorted(set(re.findall(r'Name: ((?:GLIBC|GLIBCXX|CXXABI)_[\d.]+)',ver)))
 assert all(v.startswith('GLIBC_') and tuple(map(int,v[6:].split('.')))<=(2,15) for v in versions),(p,versions)
 paths=re.findall(r'Library (?:runpath|rpath): \[(.*?)\]',dyn)
 assert all(x.startswith('$ORIGIN') for pathspec in paths for x in pathspec.split(':')),(p,paths)
 symbols=subprocess.check_output(['readelf','-Ws',str(p)],text=True)
 assert not re.search(r'\bUNIQUE\b',symbols),p
 report[p.name]={'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'needed':needed,'symbol_versions':versions,'rpath':paths,'elf32':True,'gnu_unique_symbols':False}
 if p.name=='bridge.so':
  assert re.search(r'\(HASH\)',dyn) and not re.search(r'\(GNU_HASH\)',dyn),p
  exported=subprocess.check_output(['nm','-D','--defined-only',str(p)],text=True)
  assert len(re.findall(r'__hlbridge_reserved_export_\d{4}_',exported))==4096,p
  assert not re.search(r'\b(?:worldspawn|weapon_gausspistol)$',exported,re.M),p
  report[p.name]['runtime_export_reserve']={'capacity':4096,'name_bytes':256,'hash':'sysv'}
(ROOT/'logs/elf_dependencies.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: ELF32, Scout-compatible GLIBC, static C++ runtime, relative library search, no GNU unique symbols')
