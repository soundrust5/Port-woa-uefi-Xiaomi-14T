"""Pinned LK cache-clean and display-update call chain, for host review.

The input is the exact extracted main-LK payload.  Besides disassembly, this
reconstructs the runtime module-driver table populated by 0xffff0000507482d8
and records the active scenario-3 entries used by the handoff display path.
"""
import hashlib
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).parent/'vendor/python'))
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
root=Path(__file__).resolve().parents[1]
folder=root/'evidence/extracted/2026-09-14/lk-analysis'
data=(folder/'lk-payload.bin').read_bytes()
assert hashlib.sha256(data).hexdigest()=='6afd84aefda089681f0ec4a224c6710228f525ec312bfa2532977be9e4496580'
md=Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN)
BASE=0xffff000050700000
lines=['Exact main LK; virtual addresses are not callable addresses in the probe.']
for name,start,end in [('framebuffer update tail',0x4ee08,0x4ee30),
    ('clean to PoC',0x56a4,0x56c4),('display update routine',0x504d8,0x507d8),
    ('path module start calls',0x48ed0,0x48f98),
    ('module table constructor',0x482d8,0x48308),
    ('path trigger: mutex then module callbacks',0x495b4,0x49668),
    ('mutex trigger helper',0x4cbf0,0x4cc78),
    ('DSI command queue writer, partial',0x455d8,0x45770),
    ('DSI start callback (module 43, driver +0x20)',0x46c8c,0x46e5c),
    ('DSI trigger callback (module 43, driver +0x28)',0x46e5c,0x46ff0)]:
    instructions=list(md.disasm(data[start:end],BASE+start))
    assert sum(i.size for i in instructions)==end-start
    lines+=['',name]+[f'{i.address:#x}: {i.bytes.hex()} {i.mnemonic} {i.op_str}' for i in instructions]

# The constructor copies two pointers per 0x40-byte source record into a
# contiguous 192-entry driver table.  This calculation is pinned to its exact
# loop above rather than inferred from public source.
import struct
drivers=[]
for record in range(96):
    for source in (0xe06f0 + record*0x40, 0xe0710 + record*0x40):
        drivers.append(struct.unpack_from('<Q',data,source)[0])

scenario=3
scenario_off=0xe2998 + scenario*0xc8
modules=[]
for index in range(50):
    module=struct.unpack_from('<i',data,scenario_off+index*4)[0]
    if module == -1:
        break
    modules.append(module)
assert modules == [46,43]
assert drivers[46] == 0
assert drivers[43] == 0xffff0000507df9f8
driver43=drivers[43]-BASE
assert struct.unpack_from('<Q',data,driver43+0x20)[0] == 0xffff000050746c8c
assert struct.unpack_from('<Q',data,driver43+0x28)[0] == 0xffff000050746e5c
assert struct.unpack_from('<Q',data,0xf8828+0x50)[0] == 0

for pointer_va, expected in [
    (0xffff000050806f10, 0xffff000014001000),
    (0xffff000050806f18, 0xffff000014201000),
    (0xffff000050806f20, 0xffff000014401000),
    (0xffff000050806f28, 0xffff000014601000),
]:
    pointer = struct.unpack_from('<Q', data, pointer_va-BASE)[0]
    assert struct.unpack_from('<Q', data, pointer-BASE)[0] == expected

lines += [
    '', 'reconstructed exact tables',
    'primary path creation: mov w0,#3 at 0xffff00005074e224; '
        'bl 0xffff0000507483d4 at 0xffff00005074e22c',
    'scenario 3 modules: [46, 43]',
    'module 46 driver: NULL',
    'module 43 driver object: 0xffff0000507df9f8',
    'module 43 driver +0x20 (start): 0xffff000050746c8c',
    'module 43 driver +0x28 (trigger): 0xffff000050746e5c',
    'selected n12a_36_02_0b driver: 0xffff0000507f8828',
    'selected panel driver +0x50 (update): NULL',
]
(folder/'display-update-chain.asm').write_text('\n'.join(lines)+'\n')
print('Saved exact display update chain')
