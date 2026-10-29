"""Emulate the actual leaf bytes; reject every non-whitelisted data read/write.

Run after build.sh with system Python. Unicorn is an existing isolated test dep.
This does not prove physical DRAM readability or capture a live Windows mapping.
"""
from pathlib import Path
import random
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'artifacts/degas/cpu-audit/test-deps'))
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2
from unicorn.arm64_const import UC_ARM64_REG_X18, UC_ARM64_REG_X30, UC_ARM64_REG_SP
from unicorn.arm64_const import UC_ARM64_REG_PC
import unicorn.arm64_const as armregs

CODE = (ROOT / 'artifacts/degas/prcb-walk/walk.bin').read_bytes()
ENTRY, STOP = 0x100000, 0x110000
RAM = [(0x40480000, 0x44440000), (0x140000000, 0x1BFFFE000),
       (0x1C0000000, 0x32D400000)]
VA = 0xFFFFF7B50015A020
TCR = 0x695B5513511
TABLES = [0x40480800, 0x40482000, 0x40483000, 0x40484000]
VALUE = 0xFEDCBA9876543210


def inside(address, size):
    return any(lo <= address and address + size <= hi for lo, hi in RAM)


def run(va=VA, ttbr=None, tcr=TCR, level=3, leaf_base=0x1C0010000,
        mutate=None, expected=0):
    u = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    u.mem_map(ENTRY, 0x1000)
    u.mem_write(ENTRY, CODE)
    mapped = set()

    def put(address, value):
        page = address & ~0xFFF
        if page not in mapped:
            u.mem_map(page, 0x1000)
            mapped.add(page)
        u.mem_write(address, struct.pack('<Q', value))

    entries = []
    for lev, shift in enumerate([39, 30, 21, 12]):
        idx = (va >> shift) & (0xFF if lev == 0 else 0x1FF)
        address = TABLES[lev] + idx * 8
        entries.append(address)
        if lev == level:
            put(address, leaf_base | (3 if lev == 3 else 1) | 0x400)
            break
        put(address, TABLES[lev+1] | 3)
    offset_mask = (1 << [39, 30, 21, 12][level]) - 1
    physical = leaf_base | (va & offset_mask)
    if inside(physical, 8):
        put(physical, VALUE)
    if mutate:
        mutate(put, entries)
    reads = []

    def read_hook(uc, access, address, size, value, data):
        assert inside(address, size), ('OUTSIDE RAM READ', hex(address), size)
        reads.append((address, size))

    def write_hook(uc, access, address, size, value, data):
        raise AssertionError(('WRITE', hex(address), size))

    u.hook_add(UC_HOOK_MEM_READ, read_hook)
    u.hook_add(UC_HOOK_MEM_WRITE, write_hook)
    u.reg_write(UC_ARM64_REG_X0, va)
    u.reg_write(UC_ARM64_REG_X1, TABLES[0] if ttbr is None else ttbr)
    u.reg_write(UC_ARM64_REG_X2, tcr)
    preserved = [(getattr(armregs, f'UC_ARM64_REG_X{i}'),
                  0x123456789ABCDEF0 + i) for i in range(18, 30)]
    for reg, value in preserved:
        u.reg_write(reg, value)
    u.reg_write(UC_ARM64_REG_X30, STOP)
    u.reg_write(UC_ARM64_REG_SP, 0x222222220)
    u.emu_start(ENTRY, STOP, count=1500)
    assert u.reg_read(UC_ARM64_REG_PC) == STOP, 'Did not return within instruction budget'
    result = tuple(u.reg_read(r) for r in [UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2])
    assert result[0] == expected, (result, expected)
    for reg, value in preserved:
        assert u.reg_read(reg) == value
    assert u.reg_read(UC_ARM64_REG_SP) == 0x222222220
    assert u.reg_read(UC_ARM64_REG_X30) == STOP
    if expected:
        assert result[1:] == (0, 0)
    else:
        assert result[1:] == (VALUE, physical), (result, hex(physical))
        assert reads[-1] == (physical, 8)
    assert len(reads) <= 5
    return reads


def main():
    cases = 0
    def check(**kwargs):
        nonlocal cases
        run(**kwargs)
        cases += 1

    check()  # root bit11 is essential; lower 2KiB of this page contains zeros.
    check(ttbr=(0xBEEF << 48) | TABLES[0] | 1)  # ASID/CnP stripped separately.
    check(level=1, leaf_base=0x140000000)
    check(level=2, leaf_base=0x1C0000000)
    for pa in [0x40480000, 0x4443F000, 0x140000000, 0x1BFFFD000,
               0x1C0000000, 0x32D3FF000]:
        check(leaf_base=pa)
    # Upper-region canonical addresses need NOT sign-extend bit46. Exercise
    # root indices0 and255 and the final aligned 8 bytes of each RAM interval.
    check(va=0xFFFF800000000FF8)
    check(va=0xFFFFFFFFFFFFFFF8)
    for pa in [0x4443F000, 0x1BFFFD000, 0x32D3FF000]:
        check(va=(VA & ~0xFFF) | 0xFF8, leaf_base=pa)
    for va in [0, 0x7FFFFFFFFFF8, 0xFFFF7FFFFFFFFFF8, VA+1, VA+4]:
        assert run(va=va, expected=1) == []
        cases += 1
    for t in [0, TCR ^ (1 << 30), TCR ^ (1 << 16), TCR | (1 << 23),
              TCR | (1 << 38), TCR | (1 << 59)]:
        assert run(tcr=t, expected=6) == []
        cases += 1
    for bit in range(1, 11):
        check(ttbr=TABLES[0] | (1 << bit), expected=2)
    for root in [0, 0x40000000, 0x44440000, 0x1BFFFE000, 0x1C0000000-0x800,
                 0x32D400000, 0xFFFFF800]:
        assert run(ttbr=root, expected=5) == []
        cases += 1
    for lev in range(4):
        for desc in [0, 2]:
            check(mutate=lambda put, e, l=lev, d=desc: put(e[l], d), expected=3)
        check(mutate=lambda put, e, l=lev: put(e[l], 0x40000003), expected=5)
        check(mutate=lambda put, e, l=lev: put(e[l], (1 << 48) | 3), expected=4)
    check(mutate=lambda put, e: put(e[0], 0x140000001), expected=4)
    check(level=1, leaf_base=0x140000000,
          mutate=lambda put, e: put(e[1], 0x140001401), expected=4)
    check(level=2, leaf_base=0x1C0000000,
          mutate=lambda put, e: put(e[2], 0x1C0001401), expected=4)
    check(mutate=lambda put, e: put(e[3], 0x1C0010401), expected=4)
    check(mutate=lambda put, e: put(e[1], 0x40483403 | (1 << 50)), expected=4)
    # Random leaf pointers exercise guard rejection, including secure/MMIO gaps.
    rng = random.Random(0x6897)
    for _ in range(100):
        base = rng.randrange(1 << 36) & ~0xFFF
        if not inside(base | (VA & 0xFFF), 8):
            check(leaf_base=base, expected=5)
    print(f'PASS: {cases} cases, real AArch64 bytes, <=5 guarded reads, no writes')


if __name__ == '__main__':
    main()
