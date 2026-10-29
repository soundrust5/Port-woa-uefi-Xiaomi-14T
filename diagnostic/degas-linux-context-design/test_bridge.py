"""Execute host-only bridge bytes; no phone or Windows image is modified.

MPIDR_EL1 is supplied by a hook at the fixture's MRS, since Unicorn does not
model this device's virtual CPU identities. All lookup/restore/branch
instructions execute. This does NOT test interception, publication ordering,
cache/coherency, PSCI power state, or the subsequent Windows MMU transition.
"""
from pathlib import Path
import hashlib
import json
import struct
import sys
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'tools/vendor/python'))
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm64_const import *

with (HERE / 'out/bridge.elf').open('rb') as f:
    elf = ELFFile(f)
    SYMBOLS = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
CODE = (HERE / 'out/bridge.bin').read_bytes()
BASE = SYMBOLS['DegasContextBridge']
TABLE = SYMBOLS['DegasContextTable']
FAIL = SYMBOLS['DegasContextBridgeFail']
ENTRY = 0x200000
RESULTS = []


def run_case(mpidr, rows, count=None):
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    uc.mem_map(BASE, 0x1000)
    uc.mem_write(BASE, CODE)
    uc.mem_map(ENTRY, 0x1000)
    uc.mem_write(TABLE, struct.pack('<Q', len(rows) if count is None else count))
    for i, row in enumerate(rows):
        uc.mem_write(TABLE + 8 + i * 24, struct.pack('<QQQ', *row))
    for n in range(31):
        uc.reg_write(globals()['UC_ARM64_REG_X' + str(n)], 0x5555000000000000 + n)
    uc.reg_write(UC_ARM64_REG_X0, 0)  # GZ's observed input to the AP
    uc.reg_write(UC_ARM64_REG_SP, 0x7777000000000000)
    outcome = {}
    writes = []

    def hook(_, pc, size, data):
        if pc == BASE:
            uc.reg_write(UC_ARM64_REG_X9, mpidr)
            uc.reg_write(UC_ARM64_REG_PC, BASE + 4)
        elif pc == ENTRY:
            outcome.update(ok=True, context=uc.reg_read(UC_ARM64_REG_X0),
                           entry=uc.reg_read(UC_ARM64_REG_X1))
            uc.emu_stop()
        elif pc == FAIL:
            outcome.update(ok=False)
            uc.emu_stop()

    uc.hook_add(UC_HOOK_CODE, hook)
    uc.hook_add(UC_HOOK_MEM_WRITE, lambda _, access, addr, size, value, data: writes.append((addr, size)))
    uc.emu_start(BASE, BASE + 0x1000, count=200)
    assert outcome, 'lookup exceeded bounded instruction budget'
    assert not writes, writes
    assert uc.reg_read(UC_ARM64_REG_SP) == 0x7777000000000000
    for n in range(18, 31):
        assert uc.reg_read(globals()['UC_ARM64_REG_X' + str(n)]) == 0x5555000000000000 + n
    RESULTS.append(dict(mpidr=hex(mpidr), rows=len(rows), count=count, outcome=outcome))
    return outcome


class BridgeTests(unittest.TestCase):
    def test_seven_aps_full_width_contexts(self):
        contexts = [0, 1, 0xffffffff, 0x100000000, 0xfffff78a00001860, 0xaaaaaaaa55555555, 0xffffffffffffffff]
        rows = [(i << 8, c, ENTRY) for i, c in enumerate(contexts, 1)]
        for i, context in enumerate(contexts, 1):
            with self.subTest(cpu=i):
                self.assertEqual(run_case(i << 8, rows), dict(ok=True, context=context, entry=ENTRY))

    def test_affinity_fields_and_non_affinity_flags(self):
        for mpidr in [0x100, 0x123456, 0xab00123456]:
            self.assertEqual(run_case(mpidr | 0xc1000000, [(mpidr, 0x123456789abcdef0, ENTRY)])['context'], 0x123456789abcdef0)

    def test_rejects_unknown_and_duplicate_cpu(self):
        self.assertEqual(run_case(0x200, [(0x100, 1, ENTRY)]), dict(ok=False))
        self.assertEqual(run_case(0x100, [(0x100, 1, ENTRY), (0x100, 2, ENTRY)]), dict(ok=False))

    def test_bounds_and_entry(self):
        for count in [0, 9, 0xffffffffffffffff]:
            self.assertEqual(run_case(0x100, [(0x100, 1, ENTRY)], count), dict(ok=False))
        for entry in [0, ENTRY + 1, ENTRY + 2, ENTRY + 3]:
            self.assertEqual(run_case(0x100, [(0x100, 1, entry)]), dict(ok=False))


if __name__ == '__main__':
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(BridgeTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    (HERE / 'out/results.json').write_text(json.dumps(dict(
        scope='HOST ONLY; missing Windows integration is not emulated',
        sha256=hashlib.sha256(CODE).hexdigest(), bytes=len(CODE),
        cases=RESULTS, success=result.wasSuccessful()), indent=2) + '\n')
    sys.exit(not result.wasSuccessful())
