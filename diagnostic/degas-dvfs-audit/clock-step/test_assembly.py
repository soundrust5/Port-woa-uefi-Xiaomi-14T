"""Execute assembled shared observer with modeled MRS inputs in Unicorn.

Build DvfsPmuRead.o using command in README. No target access. MRS and timer
progress are modeled; all other observer instructions execute unmodified.
"""
from pathlib import Path
import json
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / 'tools/vendor/python'),
                str(ROOT / 'artifacts/degas/cpu-audit/test-deps')]
from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
import unicorn.arm64_const as arm

with Path(__file__).with_name('DvfsPmuRead.o').open('rb') as stream:
    CODE = ELFFile(stream).get_section_by_name('.text').data()
INSNS = {i.address: i for i in Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN).disasm(CODE, 0x100000)}


def run(version=1, enabled=True, trap=None, frozen_timer=False):
    machine = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    machine.mem_map(0x100000, 4096); machine.mem_write(0x100000, CODE)
    machine.mem_map(0x200000, 4096)
    machine.reg_write(arm.UC_ARM64_REG_X0, 0x200000)
    machine.reg_write(arm.UC_ARM64_REG_X30, 0x100f00)
    state = dict(ticks=0, reads=[], writes=[], trapped=False, accelerated=False,
                 original_loop_budget=None, loop_visits=0)
    def hook(uc, address, size, _):
        ins = INSNS[address]
        if frozen_timer and ins.mnemonic == 'subs' and ins.op_str == 'x6, x6, #1':
            state['loop_visits'] += 1
            if not state['accelerated']:
                # Execute the real movz/movk initialization, prove its value,
                # then shorten only the modeled frozen-timer countdown.
                prior = [INSNS[address - offset] for offset in (8, 4)]
                assert prior[0].mnemonic in ('mov', 'movz')
                assert prior[0].op_str == 'x6, #0x9680'
                assert prior[1].mnemonic == 'movk'
                assert prior[1].op_str == 'x6, #0x98, lsl #16'
                state['original_loop_budget'] = uc.reg_read(arm.UC_ARM64_REG_X6)
                assert state['original_loop_budget'] == 0x989680
                uc.reg_write(arm.UC_ARM64_REG_X6, 3)
                state['accelerated'] = True
        if ins.mnemonic != 'mrs':
            return
        dest, name = ins.op_str.split(', ')
        state['reads'].append(name)
        if trap == name:
            state['trapped'] = True; uc.emu_stop(); return
        if name == 'cntvct_el0' and not frozen_timer:
            state['ticks'] += 1000
        values = dict(id_aa64dfr0_el1=version << 8,
                      pmcr_el0=0x41 if enabled else 0x40,
                      pmcntenset_el0=1 << 31 if enabled else 0,
                      pmccfiltr_el0=0, cntfrq_el0=13000000,
                      cntvct_el0=state['ticks'],
                      pmccntr_el0=state['ticks'] * 100 if enabled else 0)
        uc.reg_write(getattr(arm, 'UC_ARM64_REG_' + dest.upper()), values[name])
        uc.reg_write(arm.UC_ARM64_REG_PC, address + 4)
    def write(uc, access, address, size, value, _):
        state['writes'].append((address, size))
        assert 0x200000 <= address and address + size <= 0x200048
    machine.hook_add(UC_HOOK_CODE, hook)
    machine.hook_add(UC_HOOK_MEM_WRITE, write)
    machine.emu_start(0x100000, 0x100f00, count=10000)
    state['returned'] = machine.reg_read(arm.UC_ARM64_REG_PC) == 0x100f00
    state['status'] = machine.reg_read(arm.UC_ARM64_REG_X0)
    state['words'] = [int.from_bytes(machine.mem_read(0x200000+i*8, 8), 'little') for i in range(9)]
    return state


class AssemblyTests(unittest.TestCase):
    def test_no_system_register_writes(self):
        self.assertFalse(any(i.mnemonic == 'msr' for i in INSNS.values()))

    def test_success(self):
        result = run(); self.assertTrue(result['returned']); self.assertEqual(result['status'], 0)
        self.assertEqual(result['words'][:5], [0x100, 0x41, 1 << 31, 0, 13000000])
        self.assertEqual(len(result['writes']), 9)
        self.assertGreater(result['words'][7], result['words'][5])
        self.assertGreater(result['words'][8], result['words'][6])

    def test_unsupported(self):
        for version in [0, 15]:
            result = run(version=version)
            self.assertEqual(result['status'], 1); self.assertTrue(result['returned'])
            self.assertEqual(result['reads'], ['id_aa64dfr0_el1'])

    def test_disabled_is_observed_not_repaired(self):
        result = run(enabled=False)
        self.assertTrue(result['returned']); self.assertEqual(result['words'][6], 0)
        self.assertEqual(result['words'][8], 0)

    def test_trap_has_no_normal_return(self):
        result = run(trap='pmcr_el0')
        self.assertTrue(result['trapped']); self.assertFalse(result['returned'])
        self.assertEqual(len(result['writes']), 1)

    def test_frozen_reference_returns_failure_at_iteration_cap(self):
        result = run(frozen_timer=True)
        self.assertTrue(result['accelerated'])
        self.assertEqual(result['original_loop_budget'], 10000000)
        self.assertEqual(result['loop_visits'], 3)
        self.assertTrue(result['returned'])
        self.assertEqual(result['status'], 1)
        self.assertEqual(len(result['writes']), 7)


if __name__ == '__main__':
    unittest.main()
