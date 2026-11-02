#!/usr/bin/env python3
"""Reserve generic ELF symbols; no original mod DLL is inspected at build time."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
CAPACITY=4096
NAME_BYTES=256
STRIDE=16
(ROOT/'build').mkdir(exist_ok=True)
(ROOT/'build/export_slots.h').write_text(
    '#pragma once\n'+f'#define HLBRIDGE_EXPORT_CAPACITY {CAPACITY}\n'
    +f'#define HLBRIDGE_EXPORT_NAME_BYTES {NAME_BYTES}\n'
    +f'#define HLBRIDGE_EXPORT_STRIDE {STRIDE}\n')
lines=['.section .text.hlbridge_slots,"ax",@progbits', '.balign 4096',
       '.global hlbridge_slots_begin', '.hidden hlbridge_slots_begin', 'hlbridge_slots_begin:']
for i in range(CAPACITY):
    name=f'__hlbridge_reserved_export_{i:04d}_'.ljust(NAME_BYTES-1,'_')
    lines += [f'.global {name}',f'.type {name}, @function',f'{name}:',
              '.byte 0x0f, 0x0b',f'.fill {STRIDE-2}, 1, 0xcc',f'.size {name}, .-{name}']
lines += ['.global hlbridge_slots_end','.hidden hlbridge_slots_end','hlbridge_slots_end:',
          '.section .note.GNU-stack,"",@progbits']
(ROOT/'build/export_slots.S').write_text('\n'.join(lines)+'\n')
print(f'Generic ELF reserve: {CAPACITY} exports, {NAME_BYTES-1} bytes per name')
