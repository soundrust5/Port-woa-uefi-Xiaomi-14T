"""Save bounded disassembly supporting LK-HANDOFF-STATIC.md from pinned bytes."""
import hashlib
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).parent/'vendor/python'))
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN

root=Path(__file__).resolve().parents[1]
folder=root/'evidence/extracted/2026-09-14/lk-analysis'
data=(folder/'lk-payload.bin').read_bytes()
digest=hashlib.sha256(data).hexdigest()
assert digest=='6afd84aefda089681f0ec4a224c6710228f525ec312bfa2532977be9e4496580'
md=Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN)
lines=['Pinned LK SHA256 '+digest, 'Addresses are LK virtual addresses, not payload placement.']
for name,start,end in [('compression-dispatch',0xe90c,0xe99c),('gzip-header-check',0xea4c,0xea80),
    ('unknown-compression-error',0xec10,0xec1c),('kernel-reservation-and-call',0xfcd4,0xfd9c),
    ('final-handoff-call',0xffd0,0x1005c),('smc-stub',0x11398,0x113a4),
    ('physical-trampoline',0x19de4,0x19e24),('handoff-preparation',0x1a048,0x1a0bc)]:
    lines+=['',f'{name}: raw offsets [{start:#x}, {end:#x})']
    instructions=list(md.disasm(data[start:end],0xffff000050700000+start))
    assert sum(i.size for i in instructions)==end-start
    lines += [f'{i.address:#x}: {i.bytes.hex()}  {i.mnemonic} {i.op_str}' for i in instructions]
lines+=['', 'Error string at raw offset 0xb3a9c: '+repr(data[0xb3a9c:].split(b'\0')[0])]
(folder/'handoff-evidence.asm').write_text('\n'.join(lines)+'\n')
print('Saved',folder/'handoff-evidence.asm')
