"""Host-only exact-NT frequency reporting audit; never reads/writes hardware."""
from pathlib import Path
import hashlib
import json
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools/vendor/python'),
                str(ROOT / 'artifacts/degas/cpu-audit/test-deps'),
                str(ROOT / 'diagnostic/degas-windows-context-bridge')]
from bridge_fixture import source_mapped, SOURCE, SOURCE_SHA256
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import *

KB, RAM = 0x140000000, 0x46000000


def execute_save_frequency(image, frequency):
    u = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    u.mem_map(KB, len(image))
    u.mem_write(KB, bytes(image))
    u.mem_map(RAM, 0x10000)
    timer, pcr = RAM + 0x1000, RAM + 0x2000
    u.mem_write(timer + 0xc0, struct.pack('<Q', frequency))
    u.reg_write(UC_ARM64_REG_SP, RAM + 0xf000)
    u.reg_write(UC_ARM64_REG_X18, pcr)
    end = KB + len(image) - 0x1000
    u.reg_write(UC_ARM64_REG_X30, end)
    called = []

    def external_timer_find(_, pc, size, data):
        if pc == KB + 0x485fa8:
            # Only external timer lookup is modeled. The selected counter's
            # frequency is an explicit input, NOT an assumed live clock rate.
            assert u.reg_read(UC_ARM64_REG_X0) == 10
            called.append(True)
            u.reg_write(UC_ARM64_REG_X0, timer)
            u.reg_write(UC_ARM64_REG_PC, u.reg_read(UC_ARM64_REG_X30))

    u.hook_add(UC_HOOK_CODE, external_timer_find)
    u.emu_start(KB + 0x488a20, end, count=200)
    assert u.reg_read(UC_ARM64_REG_PC) == end and len(called) == 1
    prcb = pcr + 0x980
    mhz = struct.unpack('<I', u.mem_read(prcb + 0x774, 4))[0]
    raw = struct.unpack('<Q', u.mem_read(prcb + 0x778, 8))[0]
    assert mhz == (frequency + 500000) // 1000000
    assert raw == frequency
    return dict(input_timer_hz=frequency, saved_prcb_mhz=mhz,
                saved_prcb_raw_hz=raw)


if __name__ == '__main__':
    image = source_mapped()
    cases = [execute_save_frequency(image, hz) for hz in
             [13000000, 1300000000, 2200000000, 3350000000,
              1300499999, 1300500000]]
    cs = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
    ranges = dict(HalpTimerSaveProcessorFrequency=(0x488a20, 0x488a84),
                  HalpCycleCounterDiscover=(0x49fb48, 0x49fbd4),
                  HalpCycleCounterInitialize=(0x49fbf0, 0x49fc44),
                  PMCCNTR_reader=(0x203fd0, 0x203fd8))
    disassembly = {name: [f'{i.address:08X} {i.bytes.hex()} {i.mnemonic} {i.op_str}'
                          for i in cs.disasm(bytes(image[a:b]), a)]
                   for name, (a, b) in ranges.items()}
    assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == SOURCE_SHA256
    report = dict(kernel_sha256=SOURCE_SHA256, source_unchanged=True,
                  tests_passed=len(cases), cases=cases, disassembly=disassembly,
                  conclusion='The exact save routine rounds its selected timer Hz to MHz. '
                             '13MHz input yields13MHz, not1300MHz. Its timer-type10 source '
                             'is registered with a PMCCNTR_EL0 reader. '
                             'No live selected timer/PMU rate or physical CPU clock was measured.',
                  limits='Unicorn executes original NT instructions; external timer lookup modeled. '
                         'No driver, kernel file patch, frequency write, phone access or reboot.')
    out = Path(__file__).with_name('windows-frequency-test.json')
    out.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'disassembly'}, indent=2))
