"""Execute exact active NT's DPM callback-return checks; no hardware access."""
from pathlib import Path
import hashlib
import json
import struct
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools/vendor/python'),
              str(ROOT/'artifacts/degas/cpu-audit/test-deps'),
              str(ROOT/'diagnostic/degas-windows-context-bridge')]
from bridge_fixture import source_mapped, SOURCE, SOURCE_SHA256
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import *
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN

KB,RAM=0x140000000,0x46000000
image=source_mapped()
cases=[]
for name,start,normal,notification in [('prepare',0x2d0034,0x2d0040,1),
                                       ('register',0x2d0148,0x2d0164,3)]:
    for handled in [0,1]:
        u=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
        u.mem_map(KB,len(image));u.mem_write(KB,bytes(image))
        u.mem_map(RAM,0x10000)
        u.reg_write(UC_ARM64_REG_SP,RAM+0xf000)
        u.reg_write(UC_ARM64_REG_X0,handled)
        u.reg_write(UC_ARM64_REG_X19,0x1234)
        u.reg_write(UC_ARM64_REG_X20,notification)
        u.reg_write(UC_ARM64_REG_X21,RAM+0x1000)
        # Both output acceptance fields remain FALSE/NotAccepted (zero).
        u.mem_write(RAM+0x1008,b'\0')
        u.mem_write(RAM+0xf030,struct.pack('<I',0))
        result={}
        def hook(_,pc,size,data):
            if pc==KB+0x25d7c0: # Exact NT KeBugCheckEx export.
                result.update(outcome='bugcheck',code=u.reg_read(UC_ARM64_REG_X0),
                    parameters=[u.reg_read(r) for r in [UC_ARM64_REG_X1,UC_ARM64_REG_X2,
                        UC_ARM64_REG_X3,UC_ARM64_REG_X4]])
                u.emu_stop()
            elif pc==KB+normal:
                result.update(outcome='handled_decline',bugcheck=False)
                u.emu_stop()
        u.hook_add(UC_HOOK_CODE,hook)
        u.emu_start(KB+start,KB+len(image)-0x1000,count=100)
        assert result
        if handled:
            assert result['outcome']=='handled_decline'
        else:
            assert result['code']==0xa0 and result['parameters'][:2]==[0x605,notification]
        cases.append(dict(notification=name,callback_handled=handled,device_accepted=0,**result))
md=Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN)
blocks={f'{start:x}':[f'{i.address:x}: {i.mnemonic} {i.op_str}' for i in
    md.disasm(bytes(image[start:start+length]),KB+start)] for start,length in
    [(0x2d0034,0x34),(0x2d0148,0x48),(0x2ca0f0,0x24)]}
report=dict(source=str(SOURCE),sha256=SOURCE_SHA256,
            raw_sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
            exact_nt_instructions=True,callback_return_and_acceptance_are_inputs=True,
            external_calls_executed=False,cases=cases,disassembly=blocks)
out=ROOT/'artifacts/degas/dvfs-windows/nt-dpm-handled-test.json'
out.write_text(json.dumps(report,indent=2)+'\n')
print('PASS: exact NT PREPARE/REGISTER FALSE reaches A0/605; TRUE + declined ownership returns normally (4 cases)')
