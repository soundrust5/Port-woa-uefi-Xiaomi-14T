"""Compile and exercise the actual firmware locator/core, no phone access.

Run in WSL/Linux with python3. Mapped PE fixtures are private bytearrays;
original NT and all phone partitions remain untouched.
"""
import ctypes as C
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from bridge_fixture import source_mapped, PREFIX_START, prefix_words, SOURCE, sha, SOURCE_SHA256

ROOT = Path(__file__).resolve().parents[2]
PHYS = 0x50000000
VIRTUAL = 0xfffff800d3c00000
MAIL = 0x4300b000
READ = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint64, C.c_void_p, C.c_size_t)
WRITE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint64, C.c_void_p, C.c_size_t)

class Region(C.Structure):
    _fields_ = [('Base', C.c_uint64), ('Pages', C.c_uint64), ('Type', C.c_uint32), ('Attributes', C.c_uint64)]
class Domain(C.Structure):
    _fields_ = [('Read', READ), ('Write', WRITE), ('Context', C.c_void_p), ('Regions', C.POINTER(Region)), ('RegionCount', C.c_size_t)]
class Result(C.Structure):
    _fields_ = [('KernelBase', C.c_uint64), ('KernelVirtualBase', C.c_uint64),
                ('Pages', C.c_uint32), ('MzCandidates', C.c_uint32), ('Matches', C.c_uint32),
                ('Status', C.c_uint32), ('WriteAttempted', C.c_uint32), ('Patched', C.c_uint32)]

PREAMBLE = r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef uint64_t UINT64; typedef uint32_t UINT32; typedef uint16_t UINT16;
typedef uint8_t UINT8; typedef size_t UINTN; typedef int BOOLEAN; typedef void VOID;
#define STATIC static
#define CONST const
#define TRUE 1
#define FALSE 0
#define MAX_UINT64 UINT64_MAX
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define ZeroMem(p,n) memset(p,0,n)
#define CopyMem memcpy
#define MemoryFence() do{}while(0)
static UINT64 GetPerformanceCounter(void){return 123456789;}
static UINT32 retained[1024];
#define DEGAS_NATIVE_RECORD_ADDRESS ((UINTN)retained)
#include "NativeContextCore.h"
#include "Protocol/DegasNativeContext.h"
#include "NativeContextRecord.h"
void locate(const DEGAS_CONTEXT_DOMAIN*d,DEGAS_CONTEXT_RESULT*r){DegasContextLocate(d,r);}
void apply(const DEGAS_CONTEXT_DOMAIN*d,UINT64 b,DEGAS_CONTEXT_RESULT*r){DegasContextApply(d,b,r);}
void prefix(UINT64 b,UINT8*p){DegasContextPrefix(b,p);}
void record(UINT32 stage,const DEGAS_CONTEXT_RESULT*r,UINT8*out){
 DEGAS_NATIVE_CONTEXT_PROTOCOL p={1,2,0x4300b000,2,0};
 retained[0xf00/4]=0x11223344;
 DegasNativeRecord(stage,&p,r,123,0);
 memcpy(out,retained,sizeof(retained));
}
'''

class Fixture:
    def __init__(self, images, regions=None):
        self.images = {base: (C.c_ubyte * len(data)).from_buffer_copy(data) for base, data in images.items()}
        self.writes = []
        self.failed_read = None
        self.fail_write = False
        self.reads = []
        @READ
        def read(_, address, target, size):
            self.reads.append((address, size))
            if self.failed_read is not None and address <= self.failed_read < address + size:
                return 0
            for base, memory in self.images.items():
                if base <= address and address + size <= base + len(memory):
                    C.memmove(target, C.addressof(memory) + address - base, size)
                    return 1
            return 0
        @WRITE
        def write(_, address, source, size):
            self.writes.append((address, size))
            if self.fail_write:
                return 0
            for base, memory in self.images.items():
                if base <= address and address + size <= base + len(memory):
                    C.memmove(C.addressof(memory) + address - base, source, size)
                    return 1
            return 0
        self.read, self.write = read, write
        values = regions or [(base, len(data) // 4096, 1, 0) for base, data in sorted(images.items())]
        self.regions = (Region * len(values))(*[Region(*r) for r in values])
        self.domain = Domain(read, write, None, self.regions, len(values))
    def snapshot(self, base=PHYS):
        return bytes(self.images[base])

class CoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        directory = Path(cls.temp.name)
        (directory / 'core.c').write_text(PREAMBLE)
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                        '-Wno-misleading-indentation', '-fsanitize=undefined', '-fPIC', '-shared',
                        '-I', str(ROOT / 'mu/Platforms/Xiaomi/degasPkg/Include'),
                        str(directory / 'core.c'), '-o', str(directory / 'core.so')], check=True)
        cls.lib = C.CDLL(str(directory / 'core.so'))
        cls.lib.locate.argtypes = [C.POINTER(Domain), C.POINTER(Result)]
        cls.lib.apply.argtypes = [C.POINTER(Domain), C.c_uint64, C.POINTER(Result)]
        cls.lib.prefix.argtypes = [C.c_uint64, C.c_void_p]
        cls.image = source_mapped()
        struct.pack_into('<Q', cls.image, 0x200218, VIRTUAL + 0x2001fc)
    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()
    def locate(self, f):
        result = Result()
        self.lib.locate(C.byref(f.domain), C.byref(result))
        return result
    def test_exact_physical_backing_virtual_relocation_and_prefix(self):
        f = Fixture({PHYS: self.image})
        result = self.locate(f)
        self.assertEqual((result.Status, result.Matches, result.KernelBase, result.KernelVirtualBase), (0, 1, PHYS, VIRTUAL))
        self.lib.apply(C.byref(f.domain), MAIL, C.byref(result))
        self.assertEqual((result.Status, result.Patched), (0, 1))
        expected = bytearray(self.image)
        expected[PREFIX_START:PREFIX_START + 56] = struct.pack('<14I', *prefix_words(MAIL))
        self.assertEqual(f.snapshot(), expected)
        self.assertEqual(f.writes, [(PHYS + PREFIX_START, 56)])
    def test_exact_allocation_across_adjacent_loader_descriptors(self):
        split = 0x200000
        regions = [(PHYS, split // 4096, 2, 0), (PHYS + split, (len(self.image) - split) // 4096, 1, 0)]
        result = self.locate(Fixture({PHYS: self.image}, regions))
        self.assertEqual(result.Status, 0)
    def test_ambiguous_images_never_write(self):
        f = Fixture({PHYS: self.image, PHYS + 0x2000000: self.image})
        result = self.locate(f)
        self.assertEqual((result.Status, result.Matches, result.KernelBase), (4, 2, 0))
        self.lib.apply(C.byref(f.domain), MAIL, C.byref(result))
        self.assertFalse(f.writes)
    def test_near_matches_and_raw_preferred_relocation_rejected(self):
        for at in [0x114, 0x116, 0x160, 0x330, 0x3720c, 0x4dc78, 0x2000c0, 0x200150, 0x47f8f8, 0x49835c]:
            image = bytearray(self.image)
            image[at] ^= 1
            f = Fixture({PHYS: image})
            result = self.locate(f)
            self.assertEqual(result.Status, 3, hex(at))
            self.lib.apply(C.byref(f.domain), MAIL, C.byref(result))
            self.assertFalse(f.writes)
        f = Fixture({PHYS: source_mapped()})
        self.assertEqual(self.locate(f).Status, 3)
    def test_map_rejects_overlap_unsorted_overflow_or_alignment(self):
        for regions in [[(PHYS, 0, 1, 0)], [(PHYS + 1, 1, 1, 0)],
                        [(PHYS, 2, 1, 0), (PHYS + 4096, 1, 1, 0)],
                        [(PHYS + 4096, 1, 1, 0), (PHYS, 1, 1, 0)],
                        [(0xfffffffffffff000, 2, 1, 0)]]:
            f = Fixture({PHYS: self.image}, regions)
            self.assertEqual(self.locate(f).Status, 1)
            self.assertFalse(f.reads)
    def test_reserved_hole_and_mmio_are_never_read(self):
        f = Fixture({PHYS: self.image}, [(PHYS, len(self.image) // 4096, 11, 0)])
        self.assertEqual(self.locate(f).Status, 3)
        self.assertFalse(f.reads)
        regions = [(PHYS, 0x200000 // 4096, 1, 0), (PHYS + 0x200000, 1, 0, 0),
                   (PHYS + 0x201000, (len(self.image) - 0x201000) // 4096, 1, 0)]
        f = Fixture({PHYS: self.image}, regions)
        self.assertEqual(self.locate(f).Status, 3)
        self.assertTrue(all(not (address <= PHYS + 0x2000a0 < address + size) for address, size in f.reads))
        tail = 0x1000000
        regions = [(PHYS, tail // 4096, 1, 0), (PHYS + tail, 1, 0, 0),
                   (PHYS + tail + 4096, (len(self.image) - tail - 4096) // 4096, 1, 0)]
        f = Fixture({PHYS: self.image}, regions)
        self.assertEqual(self.locate(f).Status, 3)
        self.assertFalse(f.writes)
    def test_read_failures_and_scan_budget_prevent_patch(self):
        f = Fixture({PHYS: self.image})
        f.failed_read = PHYS + 4096
        self.assertEqual(self.locate(f).Status, 5)
        # A real large backing is unnecessary; an always-zero reader tests cap.
        f = Fixture({PHYS: self.image}, [(PHYS, 0x40001, 2, 0)])
        @READ
        def zero(_, address, output, size):
            C.memset(output, 0, size)
            return 1
        f.domain.Read = zero
        result = self.locate(f)
        self.assertEqual((result.Status, result.Pages), (2, 0x40000))
    def test_mid_validation_read_failure_cannot_hide_second_candidate(self):
        second = PHYS + 0x2000000
        f = Fixture({PHYS: self.image, second: self.image})
        f.failed_read = second + 0x49835c
        result = self.locate(f)
        self.assertEqual((result.Status, result.KernelBase), (5, 0))
        self.lib.apply(C.byref(f.domain), MAIL, C.byref(result))
        self.assertFalse(f.writes)
    def test_validate_again_before_write_and_reject_bad_mailboxes(self):
        f = Fixture({PHYS: self.image})
        result = self.locate(f)
        f.images[PHYS][0x200150] ^= 1
        self.lib.apply(C.byref(f.domain), MAIL, C.byref(result))
        self.assertEqual(result.Status, 6)
        self.assertFalse(f.writes)
        for mailbox in [0, 1, 0xffff9000, 0x100000000]:
            f = Fixture({PHYS: self.image})
            result = self.locate(f)
            self.lib.apply(C.byref(f.domain), mailbox, C.byref(result))
            self.assertEqual(result.Status, 7)
            self.assertFalse(f.writes)
    def test_native_prefix_immediates_match_independent_python(self):
        for mailbox in [0x1000, MAIL, 0xffff8000]:
            data = (C.c_ubyte * 56)()
            self.lib.prefix(mailbox, data)
            self.assertEqual(bytes(data), struct.pack('<14I', *prefix_words(mailbox)))
    def test_read_and_write_protection_are_not_disabled(self):
        for attr in [0x2000, 0x20000]:
            f = Fixture({PHYS: self.image}, [(PHYS, len(self.image) // 4096, 1, attr)])
            result = self.locate(f)
            if attr == 0x2000:
                self.assertEqual(result.Status, 9)
                self.assertFalse(f.reads)
            else:
                self.assertEqual(result.Status, 0)
                self.lib.apply(C.byref(f.domain), MAIL, C.byref(result))
                self.assertEqual(result.Status, 9)
            self.assertFalse(f.writes)
    def test_failed_write_is_reported_as_attempted(self):
        f = Fixture({PHYS: self.image})
        result = self.locate(f)
        f.fail_write = True
        self.lib.apply(C.byref(f.domain), MAIL, C.byref(result))
        self.assertEqual((result.Status, result.WriteAttempted, result.Patched), (8, 1, 0))
    def test_actual_retained_record_crc_decoder_and_android_flag(self):
        from decode_record import decode
        f = Fixture({PHYS: self.image})
        result = self.locate(f)
        self.lib.apply(C.byref(f.domain), MAIL, C.byref(result))
        out = (C.c_ubyte * 4096)()
        self.lib.record(3, C.byref(result), out)
        data = bytes(out)
        record = decode(data, raw_slot=True)[0]
        self.assertEqual((record['stage'], record['status'], record['patched']), (3, 0, 1))
        self.assertEqual((record['kernel_physical'], record['kernel_virtual']), (hex(PHYS), hex(VIRTUAL)))
        self.assertEqual(struct.unpack_from('<I', data, 0xf00)[0], 0x11223344)
        changed = bytearray(data)
        changed[0x40 + 88] ^= 1
        with self.assertRaises(ValueError):
            decode(changed)
        uncommitted = bytearray(data)
        uncommitted[:4] = bytes(4)
        with self.assertRaises(ValueError):
            decode(uncommitted)
        with self.assertRaises(ValueError):
            decode(uncommitted, raw_slot=True)

if __name__ == '__main__':
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(CoreTests))
    assert sha(SOURCE.read_bytes()) == SOURCE_SHA256
    report = dict(tests_run=result.testsRun, successful=result.wasSuccessful(), source_unchanged=True,
                  implementation='actual NativeContextCore.h compiled with UBSan',
                  physical_base=hex(PHYS), independent_virtual_base=hex(VIRTUAL),
                  scope='Host-owned loader domains only; no live EBS mapping or Windows integrity validation')
    Path(__file__).with_name('firmware-core-results.json').write_text(json.dumps(report, indent=2) + '\n')
    raise SystemExit(0 if result.wasSuccessful() else 1)
