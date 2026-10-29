"""Extract pinned ATF dispatch/context evidence, without executing firmware."""
import hashlib
import struct
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent/'vendor/python'))
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
root=Path(__file__).resolve().parents[1]
folder=root/'evidence/extracted/2026-09-14/atf-analysis'
data=(folder/'atf.bin').read_bytes()
assert hashlib.sha256(data).hexdigest()=='ef838aa754b367a0cfef4d18272d812895bb28aa55525b6a672245f223ca4935'
handler,id32,id64,name,counter=struct.unpack_from('<QIIQQ',data,0x67ea0)
assert (handler,id32,id64,name)==(0x4880c724,0x82000115,0xc2000115,0x48855a3d)
assert data[name-0x48800000:].split(b'\0')[0]==b'MTK_SIP_KERNEL_BOOT'
md=Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN)
lines=[f'Table at raw 0x67ea0: handler={handler:#x}, IDs={id32:#x}/{id64:#x}, name={name:#x}, last_field={counter:#x}']
for label,start,end in [('dispatch',0x2d878,0x2d8c4),('kernel-boot-handler',0xc724,0xc7c8),
    ('context-selection',0xbf44,0xc044),('return-preparation',0xd4a4,0xd5b4)]:
    ins=list(md.disasm(data[start:end],0x48800000+start))
    assert sum(i.size for i in ins)==end-start
    lines+=['',label]+[f'{i.address:#x}: {i.bytes.hex()} {i.mnemonic} {i.op_str}' for i in ins]
(folder/'handoff-evidence.asm').write_text('\n'.join(lines)+'\n')
print('ATF table and instruction extents verified')
