"""Execute exact BL2 OVL layer configuration against simulated MMIO.

This verifies address arithmetic in the shipped code, not live register state
or stage-2/IOMMU mappings. No device access.
"""
import faulthandler
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/vendor/python'))
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_MEM_WRITE
from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_SP, UC_ARM64_REG_LR

data = (ROOT/'evidence/extracted/2026-09-14/lk-analysis/bl2-ext-payload.bin').read_bytes()
sha = hashlib.sha256(data).hexdigest()
assert sha == '74f9bf4faafc694193dda1dc1f9bef40338cd389278781292d9f8044d4d3a35a'
BASE = 0xffff000078000000
MMIO = 0xffff000014402000
pointer = struct.unpack_from('<Q', data, 0x103498)[0]
assert struct.unpack_from('<Q', data, pointer-BASE)[0] == MMIO

# Constructor at 0x2e5fc copies alternating pointers from 0x40-byte records.
drivers = [struct.unpack_from('<Q', data, off)[0]
           for row in range(96)
           for off in (0xdd7e8+row*64, 0xdd808+row*64)]
assert drivers[1] == BASE+0xdf478
assert struct.unpack_from('<Q', data, drivers[1]-BASE+0x18)[0] == BASE+0x31788
scenarios = {}
for index in (0,18):
    entries = struct.unpack_from('<50i', data, 0xdfa90+index*0xc8)
    scenarios[str(index)] = list(entries[:entries.index(-1)])
    assert 1 in scenarios[str(index)]

def execute(module, layer, address, pitch, src_x=0, src_y=0):
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    # Windows Unicorn uses a handled native exception while mapping memory.
    enabled = faulthandler.is_enabled()
    faulthandler.disable()
    try:
        uc.mem_map(BASE, (len(data)+4095)&~4095)
        uc.mem_map(MMIO, 0x4000)
        uc.mem_map(0x10000000, 0x10000)
    finally:
        if enabled:
            faulthandler.enable()
    uc.mem_write(BASE, data)
    sp = 0x10008000
    # OVL layer config ABI recovered from the exact caller at 0x31994.
    args = [module, layer, 0, 0x01002002, address, src_x, src_y, pitch]
    for index, value in enumerate(args):
        uc.reg_write(UC_ARM64_REG_X0+index, value)
    # dst x/y, dimensions, key enable/value, alpha enable/value.
    stack_args = [0, 0, 1220, 2712, 0, 0, 1, 255]
    for index, value in enumerate(stack_args):
        uc.mem_write(sp+index*8, struct.pack('<Q', value))
    uc.reg_write(UC_ARM64_REG_SP, sp)
    uc.reg_write(UC_ARM64_REG_LR, BASE+0x100)
    writes = []
    def hook(_uc, _access, addr, size, value, _user):
        if MMIO <= addr < MMIO+0x4000:
            writes.append(dict(address=hex(addr), size=size, value=hex(value)))
        else:
            assert sp-0x100 <= addr and addr+size <= sp, hex(addr)
    uc.hook_add(UC_HOOK_MEM_WRITE, hook)
    uc.emu_start(BASE+0x30cf4, BASE+0x100, count=10000)
    assert uc.reg_read(UC_ARM64_REG_X0) == 0
    # Module 1 selects bank 0, layer register stride 0x20.
    regbase = MMIO+layer*0x20
    values = {int(w['address'],16): int(w['value'],16) for w in writes}
    assert values[regbase+0xf40] == address+src_y*pitch+src_x*4
    assert values[regbase+0x44]&0xffff == pitch
    return dict(module=module, layer=layer, input_address=hex(address), pitch=pitch,
                src_x=src_x, src_y=src_y, mmio_writes=writes)

cases = [execute(1,0,0xfcc2f000,4992), execute(1,1,0xfd922000,4880),
         execute(1,0,0xfcc2f000,4992,7,3)]
report = dict(payload_sha256=sha, status='exact_binary_emulation_passed', cases=cases,
              candidate_scenarios=scenarios, ovl_config_callback=hex(BASE+0x31788),
              ovl_layer_function=hex(BASE+0x30cf4),
              limits=['Module/arguments supplied from static configuration; live register values not captured.',
                      'Simulated MMIO has no DMA, IOMMU, stage-2, clocks or display panel.',
                      'Secondary layer check validates arithmetic, not that the boot-menu overlay is active.'])
(ROOT/'evidence/bl2-scanout-address-analysis.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
