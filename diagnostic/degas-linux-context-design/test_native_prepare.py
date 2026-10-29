"""Host-test the ACTUAL PrepareNativeContext function with firmware stubs.

Run under WSL/Linux: python3 diagnostic/degas-linux-context-design/test_native_prepare.py
No firmware function is copied or rewritten: the shared library includes the
current NativeContext.h directly. The test substitutes boot services, PSCI
query replies, cache publication and low-address host-owned allocation. This
does not execute an SMC, access a phone, or prove real UEFI/NVS/cache behavior.
"""
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
ACPI = ROOT / 'mu/Platforms/Xiaomi/degasPkg/Drivers/DegasAcpiDxe'
SOURCE = ACPI / 'NativeContext.h'
ERR = 1 << 63
UNSUPPORTED, NOT_READY, OUT_OF_RESOURCES, COMPROMISED = (ERR | n for n in [3, 6, 9, 33])
AFFINITY_INFO64, CPU_ON64 = 0xc4000004, 0xc4000003
CASES = 0
CASE_COUNTS = {2: 0, 8: 0}

PREAMBLE = r'''
#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <stdlib.h>
typedef uint64_t UINT64; typedef uint32_t UINT32; typedef uint16_t UINT16;
typedef uint8_t UINT8; typedef size_t UINTN; typedef int BOOLEAN; typedef void VOID;
typedef UINT64 EFI_STATUS; typedef UINT64 EFI_PHYSICAL_ADDRESS;
#define STATIC static
#define CONST const
#define EFI_SUCCESS 0ULL
#define EFI_UNSUPPORTED (0x8000000000000000ULL|3)
#define EFI_NOT_READY (0x8000000000000000ULL|6)
#define EFI_OUT_OF_RESOURCES (0x8000000000000000ULL|9)
#define EFI_COMPROMISED_DATA (0x8000000000000000ULL|33)
#define EFI_ERROR(s) (((UINT64)(s)>>63)!=0)
#define AllocateMaxAddress 1
#define EfiACPIMemoryNVS 10
#define ARM_SMC_ID_PSCI_AFFINITY_INFO_AARCH64 0xc4000004ULL
#define DEGAS_NATIVE_CONTEXT_BRIDGE 2
#ifndef TEST_BOOT_CORE
#define TEST_BOOT_CORE 0
#endif
#ifndef TEST_CPU_COUNT
#define TEST_CPU_COUNT 8
#endif
#ifndef TEST_LIMIT
#define TEST_LIMIT 2
#endif
#define DEGAS_BOOT_CORE TEST_BOOT_CORE
#define DEGAS_CPU_TEST_LIMIT TEST_LIMIT
#define DEBUG(x) do{}while(0)
typedef struct {UINT64 Arg0,Arg1,Arg2,Arg3,Arg4,Arg5,Arg6,Arg7;} ARM_SMC_ARGS;
#define mDegasFadt mSourceFadt
#include "AcpiTablesGenerated.h"
#undef mDegasFadt
#undef DEGAS_ACPI_CPU_COUNT
#define DEGAS_ACPI_CPU_COUNT TEST_CPU_COUNT
static UINT8 mDegasFadt[sizeof(mSourceFadt)];
static UINT8 mRuntimeMadt[sizeof(mDegasMadt)];
static UINT32 mEnabledCpuCount;
static UINT8 *mapping,*owned;
UINT32 host_alloc_calls,host_free_calls,host_clean_calls,host_smc_count;
UINT32 host_alloc_mode,host_alloc_type,host_alloc_memtype,host_bad_query_args;
UINT32 host_clean_zeroed,host_clean_before_publication,host_fences;
UINT64 host_alloc_pages,host_alloc_max,host_clean_address,host_clean_size;
UINT64 host_free_address,host_free_pages;
UINT64 host_smc_fn[16],host_smc_target[16],host_states[8];
static UINT16 ReadUnaligned16(const UINT16 *p){UINT16 v;memcpy(&v,p,2);return v;}
static UINT32 ReadUnaligned32(const UINT32 *p){UINT32 v;memcpy(&v,p,4);return v;}
static UINT64 ReadUnaligned64(const UINT64 *p){UINT64 v;memcpy(&v,p,8);return v;}
static VOID WriteUnaligned16(UINT16 *p,UINT16 v){memcpy(p,&v,2);}
static VOID WriteUnaligned32(UINT32 *p,UINT32 v){memcpy(p,&v,4);}
static VOID WriteUnaligned64(UINT64 *p,UINT64 v){memcpy(p,&v,8);}
static UINT8 CalculateCheckSum8(const VOID *p,UINTN n){
 const UINT8 *b=p;UINT8 sum=0;while(n--)sum=(UINT8)(sum+*b++);return (UINT8)(0-sum);
}
#define ZeroMem(p,n) memset(p,0,n)
#define MemoryFence() do{host_fences++;}while(0)
static EFI_STATUS allocate_pages(UINT32 type,UINT32 memtype,UINTN pages,EFI_PHYSICAL_ADDRESS *out){
 host_alloc_calls++;host_alloc_type=type;host_alloc_memtype=memtype;
 host_alloc_pages=pages;host_alloc_max=*out;
 if(host_alloc_mode==1)return EFI_OUT_OF_RESOURCES;
 *out=host_alloc_mode==2?0:host_alloc_mode==3?(UINT64)(uintptr_t)owned+1:
      host_alloc_mode==4?0xffff9000ULL:host_alloc_mode==5?0x100000000ULL:(UINT64)(uintptr_t)owned;
 return EFI_SUCCESS;
}
static EFI_STATUS free_pages(EFI_PHYSICAL_ADDRESS address,UINTN pages){
 host_free_calls++;host_free_address=address;host_free_pages=pages;return EFI_SUCCESS;
}
static struct {EFI_STATUS (*AllocatePages)(UINT32,UINT32,UINTN,EFI_PHYSICAL_ADDRESS*);
               EFI_STATUS (*FreePages)(EFI_PHYSICAL_ADDRESS,UINTN);} bs={allocate_pages,free_pages};
static typeof(bs) *gBS=&bs;
static VOID ArmCallSmc(ARM_SMC_ARGS *a){
 UINTN n=host_smc_count++;if(n>=16)abort();
 host_smc_fn[n]=a->Arg0;host_smc_target[n]=a->Arg1;
 if(a->Arg0!=ARM_SMC_ID_PSCI_AFFINITY_INFO_AARCH64 || a->Arg1>0x700 || (a->Arg1&255) ||
    a->Arg2 || a->Arg3 || a->Arg4 || a->Arg5 || a->Arg6 || a->Arg7){host_bad_query_args++;a->Arg0=~0ULL;return;}
 a->Arg0=host_states[a->Arg1>>8];
}
static VOID WriteBackDataCacheRange(VOID *address,UINTN size){
 UINTN i;host_clean_calls++;host_clean_address=(UINT64)(uintptr_t)address;host_clean_size=size;
 host_clean_zeroed=1;for(i=0;i<size;i++)if(((UINT8*)address)[i])host_clean_zeroed=0;
 host_clean_before_publication=1;
 for(i=0;i<8;i++)if(ReadUnaligned64((UINT64*)(mRuntimeMadt+44+80*i+24)))host_clean_before_publication=0;
}
#include "NativeContext.h"
int reset_fixture(void){
 UINTN i;
 if(!mapping){mapping=mmap(NULL,10*4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
  if(mapping==MAP_FAILED)return 0;owned=mapping+4096;
  if((uintptr_t)owned>0xffff8000ULL)return 0;
 }
 memset(mapping,0xa5,10*4096);
 memcpy(mDegasFadt,mSourceFadt,sizeof(mDegasFadt));
 memcpy(mRuntimeMadt,mDegasMadt,sizeof(mRuntimeMadt));
 for(i=0;i<8;i++){
  WriteUnaligned32((UINT32*)(mRuntimeMadt+44+80*i+12),i<TEST_LIMIT?1:0);
  host_states[i]=i?1:0;
 }
 mRuntimeMadt[9]=0;mRuntimeMadt[9]=CalculateCheckSum8(mRuntimeMadt,sizeof(mRuntimeMadt));
 mEnabledCpuCount=TEST_LIMIT;memset(&mNativeContext,0,sizeof(mNativeContext));
 host_alloc_calls=host_free_calls=host_clean_calls=host_smc_count=0;
 host_alloc_mode=host_alloc_type=host_alloc_memtype=host_bad_query_args=0;
 host_clean_zeroed=host_clean_before_publication=host_fences=0;
 host_alloc_pages=host_alloc_max=host_clean_address=host_clean_size=0;
 host_free_address=host_free_pages=0;
 memset(host_smc_fn,0,sizeof(host_smc_fn));memset(host_smc_target,0,sizeof(host_smc_target));return 1;
}
UINT64 prepare(void){return PrepareNativeContext();}
UINT64 owned_address(void){return (UINT64)(uintptr_t)owned;}
UINT64 madt_address(void){return (UINT64)(uintptr_t)mRuntimeMadt;}
UINT64 madt_size(void){return sizeof(mRuntimeMadt);}
UINT64 fadt_address(void){return (UINT64)(uintptr_t)mDegasFadt;}
UINT64 fadt_size(void){return sizeof(mDegasFadt);}
UINT64 protocol_address(void){return (UINT64)(uintptr_t)&mNativeContext;}
void enabled_count(UINT32 n){mEnabledCpuCount=n;}
void close_fixture(void){if(mapping){munmap(mapping,10*4096);mapping=owned=NULL;}}
'''


class Protocol(C.Structure):
    _fields_ = [('Version', C.c_uint32), ('Mode', C.c_uint32), ('MailboxBase', C.c_uint64),
                ('CpuCount', C.c_uint32), ('BootCore', C.c_uint32)]


class PrepareFixture:
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        path = Path(cls.directory.name)
        (path / 'fixture.c').write_text(PREAMBLE)
        cls.libraries = []
        for name, core, count, limit in [('normal', 0, 8, cls.LIMIT),
                                         ('wrong_core', 7, 8, cls.LIMIT),
                                         ('wrong_count', 0, 7, cls.LIMIT),
                                         ('wrong_limit', 0, 8, 3)]:
            out = path / (name + '.so')
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-function', '-Wno-unused-const-variable',
                            '-Wno-misleading-indentation', '-fsanitize=undefined',
                            '-fPIC', '-shared', '-DTEST_BOOT_CORE=' + str(core),
                            '-DTEST_CPU_COUNT=' + str(count), '-DTEST_LIMIT=' + str(limit),
                            '-I', str(ACPI),
                            str(path / 'fixture.c'), '-o', str(out)], check=True)
            lib = C.CDLL(str(out))
            for fn in ['prepare', 'owned_address', 'madt_address', 'madt_size',
                       'fadt_address', 'fadt_size', 'protocol_address']:
                getattr(lib, fn).restype = C.c_uint64
            lib.enabled_count.argtypes = [C.c_uint32]
            setattr(cls, name, lib)
            cls.libraries.append(lib)
    @classmethod
    def tearDownClass(cls):
        for lib in cls.libraries:
            lib.close_fixture()
        cls.directory.cleanup()
    def setUp(self):
        self.lib = self.normal
        self.assertEqual(self.lib.reset_fixture(), 1)
    def get(self, name, kind=C.c_uint32):
        return kind.in_dll(self.lib, name).value
    def set(self, name, value, kind=C.c_uint32):
        kind.in_dll(self.lib, name).value = value
    def snapshot(self, what='madt'):
        return C.string_at(getattr(self.lib, what + '_address')(), getattr(self.lib, what + '_size')())
    def change(self, offset, fmt, value, what='madt'):
        C.memmove(getattr(self.lib, what + '_address')() + offset, struct.pack(fmt, value), struct.calcsize(fmt))
    def run_prepare(self):
        global CASES
        CASES += 1
        CASE_COUNTS[self.LIMIT] += 1
        value = self.lib.prepare()
        self.assertEqual(self.get('host_bad_query_args'), 0)
        count = self.get('host_smc_count')
        functions = (C.c_uint64 * 16).in_dll(self.lib, 'host_smc_fn')
        targets = (C.c_uint64 * 16).in_dll(self.lib, 'host_smc_target')
        self.assertEqual(list(functions[:count]), [AFFINITY_INFO64] * count)
        self.assertNotIn(CPU_ON64, functions[:count])
        self.assertEqual(list(targets[:count]), [i << 8 for i in range(count)])
        return value
    def reject(self, expected, queries=None):
        before = self.snapshot()
        self.assertEqual(self.run_prepare(), expected)
        self.assertEqual(self.snapshot(), before)
        self.assertEqual(self.get('host_alloc_calls'), 0)
        self.assertEqual(self.get('host_clean_calls'), 0)
        if queries is not None:
            self.assertEqual(self.get('host_smc_count'), queries)
    def test_success_owns_eight_pages_and_publishes_selected_cpu_count(self):
        before, fadt = self.snapshot(), self.snapshot('fadt')
        self.assertEqual(self.run_prepare(), 0)
        base = self.lib.owned_address()
        self.assertEqual(self.get('host_smc_count'), 8)
        self.assertEqual((self.get('host_alloc_calls'), self.get('host_free_calls')), (1, 0))
        self.assertEqual((self.get('host_alloc_type'), self.get('host_alloc_memtype')), (1, 10))
        self.assertEqual(self.get('host_alloc_pages', C.c_uint64), 8)
        self.assertEqual(self.get('host_alloc_max', C.c_uint64), 0xffffffff)
        self.assertEqual(C.string_at(base, 0x8000), bytes(0x8000))
        for address in [base - 4096, base + 0x8000]:
            self.assertEqual(C.string_at(address, 4096), b'\xa5' * 4096)
        self.assertEqual((self.get('host_clean_calls'), self.get('host_clean_zeroed'),
                          self.get('host_clean_before_publication'), self.get('host_fences')), (1, 1, 1, 1))
        self.assertEqual(self.get('host_clean_address', C.c_uint64), base)
        self.assertEqual(self.get('host_clean_size', C.c_uint64), 0x8000)
        after = self.snapshot()
        expected = bytearray(before)
        for i in range(8):
            at = 44 + 80 * i
            struct.pack_into('<I', expected, at + 16, 0)
            struct.pack_into('<Q', expected, at + 24, base + i * 4096 if i < self.LIMIT else 0)
        expected[9] = 0
        expected[9] = -sum(expected) & 255
        self.assertEqual(after, expected)
        self.assertEqual(sum(after) & 255, 0)
        self.assertEqual(self.snapshot('fadt'), fadt)
        self.assertEqual(struct.unpack_from('<H', fadt, 129)[0], 1)
        protocol = Protocol.from_address(self.lib.protocol_address())
        self.assertEqual((protocol.Version, protocol.Mode, protocol.MailboxBase,
                          protocol.CpuCount, protocol.BootCore), (1, 2, base, self.LIMIT, 0))
    def test_build_count_revision_and_fadt_gates(self):
        for name in ['wrong_core', 'wrong_count', 'wrong_limit']:
            self.lib = getattr(self, name)
            self.assertEqual(self.lib.reset_fixture(), 1)
            self.reject(UNSUPPORTED, queries=0)
        self.lib = self.normal
        for count in [n for n in [0, 1, 2, 3, 7, 8, 9] if n != self.LIMIT]:
            self.lib.reset_fixture()
            self.lib.enabled_count(count)
            self.reject(UNSUPPORTED, queries=0)
        for revision in [0, 4, 6, 7]:
            self.lib.reset_fixture()
            self.change(8, '<B', revision)
            self.reject(UNSUPPORTED, queries=0)
        for flags in [0, 2, 3, 0xffff]:
            self.lib.reset_fixture()
            self.change(129, '<H', flags, 'fadt')
            self.reject(UNSUPPORTED, queries=0)
    def test_every_gicc_type_length_uid_interface_and_enabled_bit(self):
        for cpu in range(8):
            for offset, fmt, value in [(0, '<B', 12), (1, '<B', 79), (4, '<I', 1),
                                       (8, '<I', cpu + 8), (12, '<I', 0 if cpu < self.LIMIT else 1),
                                       (12, '<I', 3 if cpu < self.LIMIT else 2)]:
                with self.subTest(cpu=cpu, offset=offset, value=value):
                    self.lib.reset_fixture()
                    self.change(44 + cpu * 80 + offset, fmt, value)
                    self.reject(COMPROMISED, queries=cpu)
    def test_complete_affinities_reject_aliases_and_duplicates(self):
        for cpu in range(8):
            for wrong in [(cpu << 8) | 1, (cpu << 8) | (1 << 16),
                          (cpu << 8) | (1 << 32), (cpu + 8) << 8]:
                self.lib.reset_fixture()
                self.change(44 + cpu * 80 + 68, '<Q', wrong)
                self.reject(COMPROMISED, queries=cpu)
        for cpu in range(1, 8):
            self.lib.reset_fixture()
            self.change(44 + cpu * 80 + 68, '<Q', (cpu - 1) << 8)
            self.reject(COMPROMISED, queries=cpu)
    def test_every_cpu_affinity_state_is_checked_without_cpu_on(self):
        for cpu in range(8):
            for wrong in ([1, 2, 0xffffffff] if cpu == 0 else [0, 2, 0xffffffff]):
                self.lib.reset_fixture()
                (C.c_uint64 * 8).in_dll(self.lib, 'host_states')[cpu] = wrong
                self.reject(NOT_READY, queries=cpu + 1)
    def test_allocation_failures_reject_before_memory_write(self):
        for mode in range(1, 6):
            self.lib.reset_fixture()
            before = self.snapshot()
            self.set('host_alloc_mode', mode)
            self.assertEqual(self.run_prepare(), OUT_OF_RESOURCES if mode == 1 else COMPROMISED)
            self.assertEqual(self.get('host_smc_count'), 8)
            self.assertEqual(self.get('host_alloc_calls'), 1)
            self.assertEqual(self.get('host_free_calls'), 0 if mode == 1 else 1)
            self.assertEqual(self.get('host_clean_calls'), 0)
            self.assertEqual(self.snapshot(), before)
            self.assertEqual(C.string_at(self.lib.owned_address(), 0x8000), b'\xa5' * 0x8000)
            if mode != 1:
                self.assertEqual(self.get('host_free_pages', C.c_uint64), 8)


class TwoCpuPrepareTests(PrepareFixture, unittest.TestCase):
    LIMIT = 2


class EightCpuPrepareTests(PrepareFixture, unittest.TestCase):
    LIMIT = 8


if __name__ == '__main__':
    before = hashlib.sha256(SOURCE.read_bytes()).hexdigest()
    suite = unittest.TestSuite(unittest.defaultTestLoader.loadTestsFromTestCase(cls)
                               for cls in [TwoCpuPrepareTests, EightCpuPrepareTests])
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == before
    report = dict(success=result.wasSuccessful(), tests=result.testsRun, prepare_cases=CASES,
                  cpu_configurations=[2, 8], prepare_cases_by_cpu_configuration=CASE_COUNTS,
                  source=str(SOURCE), source_sha256=before, source_unchanged=True,
                  implementation='Actual PrepareNativeContext header compiled with UBSan and firmware stubs',
                  limits='Host allocation and PSCI-query/cache stubs only; no real UEFI, SMC, NVS, cache or phone validation')
    (HERE / 'out').mkdir(exist_ok=True)
    (HERE / 'out/native-prepare-results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    raise SystemExit(not result.wasSuccessful())
