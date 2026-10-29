"""Final ELF execution and independent Linux ring decoding; no reset emulation."""
import json
import os
from pathlib import Path
import struct
import sys
import unittest
import test_entry as entry

ROOT = Path(__file__).resolve().parents[2]
BUILD_NAME = os.environ.get('DEGAS_RAMOOPS_TEST_BUILD', 'build-ramoops')
assert BUILD_NAME in ('build-ramoops', 'build-ramoops-aligned')
sys.path.insert(0, str(ROOT / 'tools'))
from read_probe_ramoops import decode


class Artifacts(entry.EntryArtifacts):
    REQUIRED_SYMBOLS = entry.EntryArtifacts.REQUIRED_SYMBOLS + (
        'diagnostic_ramoops_ok', 'diagnostic_ramoops_failed', '__stack_start', '__stack_end')

    def __init__(self):
        old = entry.ELF_PATH, entry.BIN_PATH
        entry.ELF_PATH = ROOT / 'diagnostic' / BUILD_NAME / 'entry.elf'
        entry.BIN_PATH = ROOT / 'diagnostic' / BUILD_NAME / 'entry.bin'
        try:
            super().__init__()
        finally:
            entry.ELF_PATH, entry.BIN_PATH = old


class Emulation(entry.EmulationResult):
    def _hook_code(self, uc, address, size, data):
        for name in ('diagnostic_ramoops_ok', 'diagnostic_ramoops_failed'):
            if address == self.base + self.artifacts.symbols[name]:
                self.stop_symbol = name
                uc.emu_stop()
                return
        super()._hook_code(uc, address, size, data)

    def execute(self):
        self.uc.mem_map(0x48090000, 0xe0000)
        self.uc.mem_write(0x48090000, b'\xa5' * 0xe0000)
        self.uc.emu_start(self.base, self.image_end, count=200000)
        return self


class RamoopsEntryTests(unittest.TestCase):
    def test_supported_and_rejected_states(self):
        artifact = Artifacts()
        manifest = json.loads((ROOT/'diagnostic'/BUILD_NAME/'manifest.json').read_text())
        for base in entry.LOAD_BASES:
            for el, sctlr in ((4, 0), (8, 0), (4, 1), (4, 4), (8, 1 << 25), (12, 0)):
                with self.subTest(base=hex(base), el=el, sctlr=sctlr):
                    regs = {entry.DAIF: 0x3c0, entry.CURRENT_EL: el,
                            entry.SCTLR_EL1: sctlr, entry.SCTLR_EL2: sctlr,
                            entry.TCR_EL1: 0, entry.TCR_EL2: 0,
                            entry.TTBR0_EL1: 0, entry.TTBR0_EL2: 0,
                            entry.MAIR_EL1: 0, entry.MAIR_EL2: 0}
                    result = Emulation(artifact, base, regs).execute()
                    supported = el in (4, 8) and sctlr == 0
                    self.assertEqual(result.stop_symbol,
                                     'diagnostic_ramoops_ok' if supported else 'diagnostic_unsupported')
                    self.assertFalse(result.unknown_system_reads)
                    region = bytes(result.uc.mem_read(0x48090000, 0xe0000))
                    self.assertEqual(region[:0x1f000], b'\xa5' * 0x1f000)
                    self.assertEqual(region[0x5f000:], b'\xa5' * (0xe0000-0x5f000))
                    if not supported:
                        self.assertEqual(result.writes, [])
                        self.assertEqual(region, b'\xa5' * 0xe0000)
                        continue
                    for address, size, value in result.writes:
                        in_console = 0x480af000 <= address and address+size <= 0x480ef000
                        in_record_stack = base+artifact.symbols['entry_record'] <= address and address+size <= base+artifact.symbols['__stack_end']
                        self.assertTrue(in_console or in_record_stack, hex(address))
                    console = region[0x1f000:0x5f000]
                    sig, start, size = struct.unpack_from('<III', console)
                    self.assertEqual(sig, 0x43474244)
                    self.assertLessEqual(start, size)
                    self.assertLessEqual(size, len(console)-12)
                    # Exact ordering used by persistent_ram_save_old.
                    old = console[12+start:12+size] + console[12:12+start]
                    parsed = decode(old, manifest['build_id'])
                    self.assertEqual(len(parsed), 1)
                    self.assertIn(f'BASE={base:016x}', parsed[0]['text'])
                    self.assertEqual(decode(old, '0'*64), [])
                    corrupted = old.replace(b'P1 OWN_STACK', b'P9 OWN_STACK')
                    self.assertEqual(decode(corrupted, manifest['build_id']), [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
