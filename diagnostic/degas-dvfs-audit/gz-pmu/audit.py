"""Bounded offline audit of pinned GZ SYS64 routing and MDCR setup.

No source/container bytes are changed. Privileged-register inputs and the
current-VM query are modeled; original selected instructions execute in Unicorn.
Stops before unhandled paths, physical register passthrough and external logs.
"""
from pathlib import Path
import hashlib
import json
import struct
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'tools/vendor/python'))
sys.path.insert(0, str(ROOT / 'artifacts/degas/cpu-audit/test-deps'))
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import *

SOURCE = ROOT / 'evidence/raw/2026-10-29-degas-active-gz-identity/gz_b.full.img'
SHA = '011c07ed744d69a7b2760a3fb2bdd727327bec08bc844dd4177086e4ce04ee0e'
PAYLOAD_SHA = '85802b8800ce948f36f463945dd9bf3dfd348073c771133b5f7e44d1baa57fd9'
BASE, RAM = 0x38000000, 0x43000000
image = SOURCE.read_bytes()
assert hashlib.sha256(image).hexdigest() == SHA
payload = image[0x200:0x200 + 0x1b77c0]
assert hashlib.sha256(payload).hexdigest() == PAYLOAD_SHA
md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
md.skipdata = True


def cpu():
    u = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    u.mem_map(BASE, 0x200000)
    u.mem_write(BASE, payload)
    u.mem_map(RAM, 0x10000)
    u.reg_write(UC_ARM64_REG_SP, RAM + 0xf000)
    return u


def sys64_iss(op0, op1, crn, crm, op2, *, read=True, rt=0):
    return (op0 << 20) | (op2 << 17) | (op1 << 14) | (crn << 10) | (rt << 5) | (crm << 1) | int(read)


def dispatch(name, encoding, gzvm, stop):
    u = cpu()
    esr = (0x18 << 26) | (1 << 25) | encoding
    u.reg_write(UC_ARM64_REG_X0, RAM)
    stops, external = [], []

    def hook(_, pc, size, data):
        offset = pc - BASE
        if offset == stop:
            stops.append(offset)
            u.emu_stop()
        elif offset == 0x3ce88: # MRS ESR_EL2: explicit modeled trap input
            u.reg_write(UC_ARM64_REG_X0, esr)
            u.reg_write(UC_ARM64_REG_PC, pc + 4)
        elif offset == 0x35a8c: # bounded external current-VM lookup
            external.append('current_VM_query')
            u.reg_write(UC_ARM64_REG_X0, 1 if gzvm else 0)
            u.reg_write(UC_ARM64_REG_PC, u.reg_read(UC_ARM64_REG_LR))

    u.hook_add(UC_HOOK_CODE, hook)
    u.emu_start(BASE + 0x3ce60, BASE + len(payload), count=500)
    assert stops == [stop], (name, gzvm, hex(u.reg_read(UC_ARM64_REG_PC)))
    assert bytes(u.mem_read(BASE, len(payload))) == payload
    return dict(register=name, vm_route='GZVM' if gzvm else 'Linux/no_VM',
                esr=hex(esr), masked_sysreg_key=hex(encoding & 0x3ffc1e),
                stop_offset=hex(stop), external_helpers_modeled=external,
                stop_reason='positive non-PMU match before register access' if name.startswith('CONTROL')
                            else 'unhandled SYS64 path before logs/abort helpers')


def mdcr_case(old, n):
    u = cpu()
    written = []
    def hook(_, pc, size, data):
        offset = pc - BASE
        if offset == 0x3b534:
            u.reg_write(UC_ARM64_REG_X1, n << 11)
            u.reg_write(UC_ARM64_REG_PC, pc + 4)
        elif offset == 0x3b538:
            u.reg_write(UC_ARM64_REG_X0, old)
            u.reg_write(UC_ARM64_REG_PC, pc + 4)
        elif offset == 0x3b548:
            written.append(u.reg_read(UC_ARM64_REG_X0))
            u.reg_write(UC_ARM64_REG_PC, pc + 4)
        elif offset == 0x3b54c:
            u.emu_stop()
    u.hook_add(UC_HOOK_CODE, hook)
    u.emu_start(BASE + 0x3b534, BASE + len(payload), count=20)
    assert written == [(old & 0xffffffe0) | n]
    assert written[0] & 0x60 == old & 0x60 # TPM/TPMCR are inherited
    return dict(input_mdcr=hex(old), pmcr_N=n, output_mdcr=hex(written[0]),
                preserves_TPM_TPMCR=True)


if __name__ == '__main__':
    register_hits = []
    for i in md.disasm(payload, 0):
        if i.mnemonic in ('mrs', 'msr') and any(s in i.op_str for s in (
            'pmcc', 'pmcr', 'mdcr', 'pmselr', 'pmev', 'pmcnt', 'pmint', 'pmuser', 'pmov')):
            register_hits.append(dict(offset=hex(i.address), bytes=i.bytes.hex(),
                                      mnemonic=i.mnemonic, operands=i.op_str))
    assert [(h['offset'], h['mnemonic'], h['operands']) for h in register_hits] == [
        ('0x3b534', 'mrs', 'x1, pmcr_el0'), ('0x3b538', 'mrs', 'x0, mdcr_el2'),
        ('0x3b548', 'msr', 'mdcr_el2, x0')]
    index = 0x18 - 1
    table_entry = struct.unpack_from('<h', payload, 0x79d10 + index * 2)[0]
    target = 0x3cf5c + table_entry * 4
    assert target == 0x3d060
    cases = []
    for gzvm in (False, True):
        for name, register in [('PMCCNTR_EL0', (3, 3, 9, 13, 0)), ('PMCR_EL0', (3, 3, 9, 12, 0))]:
            for read in (True, False):
                cases.append(dispatch(name + ('_read' if read else '_write'),
                                      sys64_iss(*register, read=read, rt=0 if read else 7),
                                      gzvm, 0x379bc if gzvm else 0x3d474))
    cases.append(dispatch('CONTROL_ICC_SGI1R_EL1', sys64_iss(3, 0, 12, 11, 5, read=False), False, 0x3d5a4))
    cases.append(dispatch('CONTROL_ID_AA64MMFR2_EL1', sys64_iss(3, 0, 0, 7, 2), True, 0x37bcc))
    controls = [mdcr_case(old, n) for old in (0, 0x60, 0x20065, 0x10080000065)
                for n in (0, 6, 31)]
    windows = [(0x3b4f0, 0x3b570), (0x3ce60, 0x3ce9c), (0x3cf44, 0x3cf60),
               (0x3d060, 0x3d084), (0x3d3e0, 0x3d490), (0x3d5a4, 0x3d604),
               (0x3d678, 0x3d6c0), (0x37924, 0x37c5c)]
    lines = ['Original pinned GZ bytes only; payload-relative addresses.']
    for begin, end in windows:
        lines.append('')
        for i in md.disasm(payload[begin:end], begin):
            lines.append(f'{i.address:06x}: {i.bytes.hex()} {i.mnemonic} {i.op_str}')
    (HERE / 'original-disassembly.txt').write_text('\n'.join(lines) + '\n')
    report = dict(source=str(SOURCE.relative_to(ROOT)), source_sha256=SHA,
                  payload_offset='0x200', payload_bytes='0x1b77c0', payload_sha256=PAYLOAD_SHA,
                  linked_base=hex(BASE), static_PMU_register_hits=register_hits,
                  SYS64_EC='0x18', SYS64_table='0x79d10', SYS64_table_index=index,
                  SYS64_halfword=hex(table_entry), SYS64_dispatch_target=hex(target),
                  original_instruction_dispatch_cases=cases, original_instruction_MDCR_cases=controls,
                  outcome='No PMCCNTR scaling/emulation implementation identified in inspected core routes. Ultimate source of Windows1300MHz remains UNKNOWN.',
                  limits='Static core-payload scan and bounded original-instruction paths; ESR/PMCR/MDCR inputs and current-VM query modeled. Stops before hardware/register passthrough or abort/log helpers. No live register values, modules/runtime modifications, secure-world state, PMU rate, physical clock or Windows calibration measurement.',
                  firmware_modified=False, phone_operations=False)
    assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == SHA
    (HERE / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS:', len(cases), 'SYS64 cases and', len(controls), 'MDCR cases; original bytes unchanged')
