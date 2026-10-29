"""Independent table invariants and actual pinned Windows package walker.

No phone or firmware writes. Only host fixture memory is changed. Package
walker tests execute original NT instructions, including its PPTT offset
validator; they do not emulate complete HAL initialization or Task Manager.
"""
from pathlib import Path
import hashlib
import json
import struct
import sys
import unittest

from pptt_design import *

ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'tools/vendor/python'))
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import *

SOURCE = ROOT / 'artifacts/degas/cpu-audit/ntoskrnl.exe'
SOURCE_SHA = '5bb49f8383e38b26ef0c18f3087db3be541e99143b80b92d2f83d824b7e5301c'
BASE, TABLE, RAM = 0x140000000, 0x60000000, 0x61000000
RESULTS = []


def original_code(rva, size):
    raw = SOURCE.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == SOURCE_SHA
    pe = struct.unpack_from('<I', raw, 60)[0]
    count, opt_size = struct.unpack_from('<H', raw, pe + 6)[0], struct.unpack_from('<H', raw, pe + 20)[0]
    for i in range(count):
        at = pe + 24 + opt_size + i * 40
        virtual_size, virtual, raw_size, file_offset = struct.unpack_from('<IIII', raw, at + 8)
        if virtual <= rva and rva + size <= virtual + min(virtual_size, raw_size):
            offset = file_offset + rva - virtual
            return raw[offset:offset + size]
    raise ValueError('code outside one file-backed section')


def walk_package(table, leaf):
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    uc.mem_map(BASE + 0x4A0000, 0x2000)
    uc.mem_write(BASE + 0x4A0000, original_code(0x4A0000, 0x2000))
    uc.mem_map(BASE + 0xC10000, 0x1000)
    uc.mem_write(BASE + 0xC105A8, struct.pack('<Q', TABLE))
    uc.mem_map(TABLE, 0x1000)
    uc.mem_write(TABLE, table)
    uc.mem_map(RAM, 0x2000)
    uc.reg_write(UC_ARM64_REG_PSTATE, 5)
    uc.reg_write(UC_ARM64_REG_SP, RAM + 0x1000)
    uc.reg_write(UC_ARM64_REG_X0, TABLE + leaf)
    uc.reg_write(UC_ARM64_REG_X1, RAM)
    uc.reg_write(UC_ARM64_REG_X30, RAM + 0x1800)
    done = False

    def hook(_, pc, size, data):
        nonlocal done
        if pc == RAM + 0x1800:
            done = True
            uc.emu_stop()
    uc.hook_add(UC_HOOK_CODE, hook)
    uc.emu_start(BASE + 0x4A07E8, RAM + 0x1804, count=200)
    assert done, 'original package walk exceeded instruction budget'
    return uc.reg_read(UC_ARM64_REG_X0), struct.unpack('<Q', uc.mem_read(RAM, 8))[0]


class DesignTests(unittest.TestCase):
    def test_exact_flat_package_both_revisions(self):
        for revision in (2, 3):
            data = build_topology_only(revision)
            nodes = validate_topology_only(data)
            self.assertEqual(len(nodes), 9)
            self.assertEqual(sum(n['flags'] & 1 for n in nodes.values()), 1)
            self.assertFalse(any(n['flags'] & (4 | 16) for n in nodes.values()))
    def test_original_windows_package_walk_groups_all_eight(self):
        for revision in (2, 3):
            data = build_topology_only(revision)
            for cpu in range(8):
                status, package = walk_package(data, 0x38 + cpu * 20)
                self.assertEqual((status, package), (0, TABLE + ROOT_OFFSET))
                RESULTS.append(dict(revision=revision, uid=cpu, status=status, package=hex(package)))
    def test_reject_bad_checksum_parent_flags_uid_and_cache_node(self):
        original = build_topology_only()
        for offset, fmt, value in [(0x38 + 8, '<I', 0x38), (0x38 + 4, '<I', 0xB),
                                   (0x24 + 4, '<I', 0x11), (0x4C + 12, '<I', 0),
                                   (0x38, '<B', 1), (0x38 + 16, '<I', 1)]:
            data = bytearray(original)
            struct.pack_into(fmt, data, offset, value)
            data[9] = 0
            data[9] = -sum(data) & 255
            with self.assertRaises(ValueError):
                validate_topology_only(data)
        data = bytearray(original)
        data[9] ^= 1
        with self.assertRaises(ValueError):
            validate_topology_only(data)
    def test_ccidx_formats_and_no_sharing_inference(self):
        for ccidx, ways, sets in [(0, 4, 256), (1, 1025, 65537)]:
            raw = 2 | ((ways - 1) << 3) | ((sets - 1) << (32 if ccidx else 13))
            value = ccsidr_maintenance_geometry(raw, ccidx << 20)
            self.assertEqual((value['line_bytes'], value['ways'], value['sets']), (64, ways, sets))
            self.assertEqual(value['maintenance_capacity_product'], 64 * ways * sets)
            self.assertFalse(value['sharing_proven'])
            self.assertFalse(value['actual_cache_size_proven'])
        with self.assertRaises(ValueError):
            ccsidr_maintenance_geometry(0, 2 << 20)
    def test_only_valid_clidr_selectors(self):
        self.assertEqual(clidr_selectors(3 | (4 << 3) | (4 << 6)),
                         [(1, 'data', 0), (1, 'instruction', 1), (2, 'unified', 2), (3, 'unified', 4)])
        self.assertEqual(clidr_selectors(1), [(1, 'instruction', 1)])
        self.assertEqual(clidr_selectors(2), [(1, 'data', 0)])
        self.assertEqual(clidr_selectors(0), [])
        for invalid in (5, 6, 7):
            with self.assertRaises(ValueError):
                clidr_selectors(invalid)


if __name__ == '__main__':
    before = hashlib.sha256(SOURCE.read_bytes()).hexdigest()
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(DesignTests))
    assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == before == SOURCE_SHA
    out = HERE / 'out'
    out.mkdir(exist_ok=True)
    (out / 'host-tests.json').write_text(json.dumps(dict(success=result.wasSuccessful(),
        tests=result.testsRun, original_windows_package_walk_cases=RESULTS,
        source_sha256=SOURCE_SHA, source_unchanged=True,
        limits='Actual pinned package walker/offset validator only; no HAL initialization, Windows boot or hardware cache proof'), indent=2) + '\n')
    raise SystemExit(not result.wasSuccessful())
