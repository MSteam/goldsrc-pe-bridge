#!/usr/bin/env python3
from pathlib import Path
import json,hashlib,subprocess
ROOT=Path(__file__).resolve().parents[1];BRIDGE=ROOT.parent
image=json.loads((ROOT/'logs/sdk_image.json').read_text())[0]
artifacts=json.loads((ROOT/'logs/elf_dependencies.json').read_text())
report={'runtime':'scout','sdk_image_id':image['Id'],'sdk_digest':image['Digest'],
 'sdk_created':image['Created'],'compiler':'SteamRT gcc-9/g++-9 (9.3.1)',
 'architecture':'i386','cxx_runtime':'static, supplied by Scout GCC 9',
 'hl_sdk_commit':subprocess.check_output(['git','-C',str(BRIDGE/'halflife'),'rev-parse','HEAD'],text=True).strip(),
 'loader_commit':'c40033b12fd82514f8ee4e42a25906b773b91b5c',
 'loader_patch_sha256':hashlib.sha256((BRIDGE/'pe_runtime/tools/loadlibrary.patch').read_bytes()).hexdigest(),
 'settings':(BRIDGE/'build_settings.mk').read_text(),
 'windows_helper':'PE32 built with host clang-cl/lld-link; no ELF dependencies',
 'artifacts':artifacts}
text=json.dumps(report,indent=2)+'\n'
(ROOT/'logs/build_environment.json').write_text(text)
(ROOT/'work/src/build_environment.json').write_text(text)
print('Recorded Scout SDK and artifact provenance.')
