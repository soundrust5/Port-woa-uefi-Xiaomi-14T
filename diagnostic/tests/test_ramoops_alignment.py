"""Regression: enforce natural data alignment missing from default Unicorn."""
import json
from pathlib import Path
import unittest
import test_entry as entry
import test_ramoops_entry as ramoops
from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm64_const import UC_ARM64_REG_PC, UC_ARM64_REG_SP


class AlignmentTests(unittest.TestCase):
    def test_old_image_fails_before_console_and_aligned_image_passes(self):
        root = Path(__file__).resolve().parents[2]
        reports = []
        old_root = ramoops.ROOT
        for build in ('build-ramoops', 'build-ramoops-aligned'):
            old = entry.ELF_PATH, entry.BIN_PATH
            entry.ELF_PATH = root/'diagnostic'/build/'entry.elf'
            entry.BIN_PATH = root/'diagnostic'/build/'entry.bin'
            class Artifact(entry.EntryArtifacts):
                REQUIRED_SYMBOLS = ramoops.Artifacts.REQUIRED_SYMBOLS
            try:
                artifact = Artifact()
            finally:
                entry.ELF_PATH, entry.BIN_PATH = old
            for base in entry.LOAD_BASES:
                regs = {entry.DAIF:0x3c0, entry.CURRENT_EL:4, entry.SCTLR_EL1:0x30d00800,
                        entry.TCR_EL1:0, entry.TTBR0_EL1:0, entry.MAIR_EL1:0}
                result = ramoops.Emulation(artifact, base, regs)
                faults = []
                def check(uc, access, address, size, value, user):
                    if size > 1 and address % size:
                        faults.append(dict(pc=hex(uc.reg_read(UC_ARM64_REG_PC)),
                                           address=hex(address), size=size,
                                           sp=hex(uc.reg_read(UC_ARM64_REG_SP))))
                        uc.emu_stop()
                result.uc.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE, check)
                result.execute()
                if build == 'build-ramoops':
                    self.assertEqual(len(faults),1)
                    self.assertEqual(faults[0]['pc'],hex(base+0x604))
                    self.assertEqual(faults[0]['sp'],hex(base+0x48b0))
                    self.assertFalse(any(0x480af000<=a<0x480ef000 for a,_,_ in result.writes))
                else:
                    self.assertEqual(faults,[])
                    self.assertEqual(result.stop_symbol,'diagnostic_ramoops_ok')
                reports.append(dict(build=build,base=hex(base),faults=faults,stop=result.stop_symbol))
        (root/'evidence/ramoops-alignment-tests.json').write_text(json.dumps(reports,indent=2)+'\n')


if __name__ == '__main__':
    unittest.main(verbosity=2)
