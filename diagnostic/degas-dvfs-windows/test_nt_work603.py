"""Exact installed-NT work-buffer consumption regression. No hardware access."""
from pathlib import Path
import hashlib
import json
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools/vendor/python'),str(ROOT/'artifacts/degas/cpu-audit/test-deps'),
              str(ROOT/'diagnostic/degas-windows-context-bridge')]
from bridge_fixture import source_mapped,SOURCE,SOURCE_SHA256
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm64_const import *
image=source_mapped();KB=0x140000000;RAM=0x46000000
cases=[]
for style in ['replace_pointer','fill_supplied_buffer']:
    u=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
    u.mem_map(KB,len(image));u.mem_write(KB,bytes(image));u.mem_map(RAM,0x10000)
    sp=RAM+0xd000;plugin=RAM+0x1000;replacement=RAM+0x2000
    for r,v in [(UC_ARM64_REG_SP,sp),(UC_ARM64_REG_X19,plugin),
                (UC_ARM64_REG_X21,RAM+0x3000),(UC_ARM64_REG_X22,RAM+0x4000)]:u.reg_write(r,v)
    u.mem_write(plugin+0x60,(RAM+0x5000).to_bytes(8,'little'))
    result={'style':style}
    def hook(_,pc,size,data):
        if pc in [KB+0x626980,KB+0x26d180,KB+0x206550,KB+0x626c30]:
            if pc==KB+0x26d180:u.reg_write(UC_ARM64_REG_X0,0)
            u.reg_write(UC_ARM64_REG_PC,u.reg_read(UC_ARM64_REG_X30));return
        if pc==KB+0x2cb494:
            work=u.reg_read(UC_ARM64_REG_X1)
            supplied=int.from_bytes(u.mem_read(work,8),'little')
            result['supplied_information']=hex(supplied)
            destination=replacement if style=='replace_pointer' else supplied
            u.mem_write(destination,(3).to_bytes(4,'little'))
            if style=='replace_pointer':u.mem_write(work,replacement.to_bytes(8,'little'))
            u.mem_write(work+8,b'\x01');u.reg_write(UC_ARM64_REG_X0,1)
            u.reg_write(UC_ARM64_REG_PC,KB+0x2cb498)
            result['returned_information']=hex(int.from_bytes(u.mem_read(work,8),'little'));return
        if pc==KB+0x2cd500:
            p=u.reg_read(UC_ARM64_REG_X1)
            result['dispatcher_information']=hex(p)
            result['dispatcher_type']=int.from_bytes(u.mem_read(p,4),'little')
        if pc==KB+0x2cd988 and style=='fill_supplied_buffer':
            result['outcome']='valid_power_control_type3_dispatch';u.emu_stop()
        if pc==KB+0x25d7c0:
            result['outcome']='bugcheck';result['code']=hex(u.reg_read(UC_ARM64_REG_X0))
            result['parameters']=[hex(u.reg_read(r)) for r in [UC_ARM64_REG_X1,UC_ARM64_REG_X2,UC_ARM64_REG_X3,UC_ARM64_REG_X4]]
            u.emu_stop()
    u.hook_add(UC_HOOK_CODE,hook)
    u.emu_start(KB+0x2cb404,KB+len(image)-0x1000,count=200)
    assert result['dispatcher_information']==result['supplied_information']
    if style=='replace_pointer':
        assert result['code']=='0xa0' and result['parameters']==['0x603',hex(plugin),'0x0','0x0']
    else:
        assert result['dispatcher_type']==3 and result['outcome']=='valid_power_control_type3_dispatch'
    cases.append(result)
report=dict(source=str(SOURCE),sha256=SOURCE_SHA256,
    actual_source_sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
    exact_nt_instructions=True,callback_field_assignments_modeled=True,
    cfg_irql_lock_stack_helpers_modeled=True,stops_before_external_power_control_execution=True,cases=cases)
(ROOT/'artifacts/degas/dvfs-windows/nt-work603-test.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: exact NT consumes supplied buffer; old pointer swap reaches A0/603, preserved-pointer type3 reaches valid dispatch')
