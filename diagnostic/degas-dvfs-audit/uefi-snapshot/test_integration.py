"""Actual CpuDvfsSnapshot.h integration, mocked AT/MMIO/UI/retained zone only.

Run in WSL/Linux. No shared firmware source or phone is modified.
"""
import ctypes as C
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_snapshot import UEFI, PRINT, Snapshot
from decode_snapshot import parse_journal, decode_frame

# The new raw PMU journal uses EDK2 %Lu as well as %Lx.
PRINT = PRINT.replace("format[i+1]=='x'", "(format[i+1]=='x' || format[i+1]=='u')")

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
FIRMWARE = ROOT / 'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe'
STUBS = r'''
#include <string.h>
#include <stdlib.h>
typedef uint8_t UINT8;
typedef struct {int unused;} EFI_GRAPHICS_OUTPUT_PROTOCOL;
#define DEBUG_ERROR 0
#define DEBUG(x) do{}while(0)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define CopyMem memcpy
#define ZeroMem(p,n) memset(p,0,n)
#define AsciiStrLen strlen
static UINT32 zone[1024];
#define DEGAS_APJ_BASE ((UINTN)zone)
#define DEGAS_APJ_WORDS 1024U
static UINT32 fences,corrupt_after_commit,qualifications,reads,qualified_fault,qualify_mode,watchdogs;
static UINT32 pmu_calls,pmu_status,pmu_saw_committed,writes;
static char commits[4][2048];static UINT32 commit_count;
static UINT64 ArmReadCurrentEL(void){return 4;}
static UINT64 ArmReadMpidr(void){return 0;}
static UINT64 GetPerformanceCounter(void){return 0;}
static UINT64 GetTimeInNanoSecond(UINT64 t){return t;}
static void MicroSecondDelay(UINTN us){(void)us;}
static void CpuDeadLoop(void){abort();}
static void MmioWrite32(UINTN a,UINT32 v){(void)a;(void)v;++writes;}
UINT32 DegasDvfsPmuRead(UINT64*out){
 ++pmu_calls;
 pmu_saw_committed=zone[0]==0x43474244U;
 out[0]=0x100;out[1]=0x41;out[2]=0x80000000;out[3]=0;
 out[4]=13000000;out[5]=1000;out[6]=100000;
 out[7]=131000;out[8]=13100000;
 return pmu_status;
}
static void fence(void){
 ++fences;
 if(corrupt_after_commit && fences==3)zone[20]^=1;
 if(zone[0]==0x43474244U && commit_count<4 && zone[1]<2037){
  memcpy(commits[commit_count],(char*)zone+12,zone[1]);
  commits[commit_count++][zone[1]]=0;
 }
}
#define MemoryFence() fence()
static void BootWatchdogArmTenSeconds(void){++watchdogs;}
static void DegasApJournalRead(UINT32*out){memcpy(out,zone,sizeof(zone));}
static int DegasApJournalReportAndDump(const UINT32*p){(void)p;return 1;}
static void Label(EFI_GRAPHICS_OUTPUT_PROTOCOL*g,UINTN y,const char*s){(void)g;(void)y;(void)s;}
UINT64 DegasNativeTranslate(UINT64 a,UINT32 write){
 UINT32 slot=qualifications++;(void)write;
 if(slot==qualified_fault || qualify_mode==1)return 1;
 if(qualify_mode==2)return (a&~4095ULL)+4096;
 return (a&~4095ULL)|0xff00000000000000ULL;
}
static UINT32 MmioRead32(UINTN a){(void)a;++reads;return 0xffffffffU;}
#include "CpuDvfsSnapshot.h"
void reset(void){
 memset(zone,0xa5,sizeof(zone));memset(mDvfsJournal,0,sizeof(mDvfsJournal));
 memcpy((char*)mDvfsJournal+12,"====0.000000-D\n",15);
 mDvfsJournalPosition=15;mDvfsJournalComplete=1;
 fences=corrupt_after_commit=qualifications=reads=qualify_mode=watchdogs=0;
 pmu_calls=pmu_status=pmu_saw_committed=writes=0;
 commit_count=0;memset(commits,0,sizeof(commits));
 qualified_fault=109;
}
void capture_menu(void){CpuDvfsSnapshot(NULL);}
void set_fault(UINT32 slot,UINT32 mode){qualified_fault=slot;qualify_mode=mode;}
UINT32 capture_only(DEGAS_DVFS_SNAPSHOT*out){return DegasDvfsCaptureUefi(DvfsQualify,DvfsRead,NULL,2,1,out);}
UINT32 qualify_address(UINT64 a){return DvfsQualify(NULL,a);}
void emit_bytes(UINT32 n){char b[4097];if(n>4096)abort();memset(b,'X',n);b[n]=0;DvfsEmit(NULL,b);}
UINT32 publish(void){return DvfsPublish();}
void inject_corruption(void){corrupt_after_commit=1;}
void set_position(UINTN n){mDvfsJournalPosition=n;}
UINTN used(void){return mDvfsJournalPosition;}
UINT32 complete(void){return mDvfsJournalComplete;}
UINT32 get_reads(void){return reads;}
UINT32 get_quals(void){return qualifications;}
UINT32 get_watchdogs(void){return watchdogs;}
UINT32 get_pmu_calls(void){return pmu_calls;}
UINT32 get_pmu_committed(void){return pmu_saw_committed;}
UINT32 get_writes(void){return writes;}
const char* get_commit(UINT32 n){return commits[n];}
void set_pmu_status(UINT32 s){pmu_status=s;}
void copy_zone(void*p){memcpy(p,zone,sizeof(zone));}
'''


class Integration(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='degas-dvfs-integration-')
        directory = Path(cls.tmp.name)
        (directory / 'Library').mkdir()
        (directory / 'Uefi.h').write_text(UEFI)
        (directory / 'Library/PrintLib.h').write_text(PRINT)
        (directory / 'actual.c').write_text('#include "Uefi.h"\n' + STUBS)
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-function', '-shared', '-fPIC', '-fsanitize=undefined',
                        '-fno-sanitize-recover=undefined', '-I', str(directory),
                        '-I', str(FIRMWARE), str(directory / 'actual.c'),
                        '-o', str(directory / 'actual.so')], check=True)
        cls.lib = C.CDLL(str(directory / 'actual.so'))
        cls.lib.copy_zone.argtypes = [C.c_void_p]
        cls.lib.used.restype = C.c_size_t
        cls.lib.capture_only.argtypes = [C.POINTER(Snapshot)]
        cls.lib.qualify_address.argtypes = [C.c_uint64]
        cls.lib.set_position.argtypes = [C.c_size_t]
        cls.lib.set_fault.argtypes = [C.c_uint32, C.c_uint32]
        cls.lib.get_commit.restype = C.c_char_p

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.lib.reset()

    def zone(self):
        output = (C.c_ubyte * 4096)()
        self.lib.copy_zone(output)
        return bytes(output)

    def test_clock_gate_failure_never_writes(self):
        self.lib.capture_menu()
        self.assertEqual(self.lib.get_writes(), 0)

    def test_full_real_integration_fits_and_preserves_flag_and_upper_half(self):
        before = self.zone()
        self.lib.capture_menu()
        after = self.zone()
        self.assertEqual(after[2048:], before[2048:])
        self.assertEqual(after[0xf00:0xf10], before[0xf00:0xf10])
        count = self.lib.used()
        self.assertLess(count, 1800)
        self.assertEqual(after[:4], b'DBGC')
        self.assertEqual(int.from_bytes(after[4:8], 'little'), count)
        self.assertEqual(int.from_bytes(after[8:12], 'little'), count)
        text = after[12:12 + count].decode()
        self.assertIn('CLK1 end status=1', text)
        frames = parse_journal(self.lib.get_commit(0).decode())
        self.assertEqual(len(frames), 1)
        report = decode_frame(frames[0])
        self.assertEqual((report['stage'], report['successful_reads']), ('UEFI menu', 109))
        self.assertFalse(report['physical_clock_measured'])
        self.assertEqual((self.lib.get_quals(), self.lib.get_reads(), self.lib.get_watchdogs()), (109, 109, 1))
        self.assertEqual((self.lib.get_pmu_calls(), self.lib.get_pmu_committed()), (1, 1))
        pmu_commit = self.lib.get_commit(1).decode()
        self.assertIn('PMU1 status=0 dfr=0000000000000100 cr=0000000000000041', pmu_commit)
        self.assertIn('PMU1 refhz=13000000', pmu_commit)
        self.assertNotIn('MHz', text)

    def test_pmu_unavailable_is_committed_before_clock_summary_without_clock_claim(self):
        self.lib.set_pmu_status(1)
        self.lib.capture_menu()
        count = self.lib.used()
        text = self.zone()[12:12 + count].decode()
        self.assertIn('PMU1 status=1', self.lib.get_commit(1).decode())
        self.assertIn('CLK1 end status=1', text)
        self.assertNotIn('physical_clock_measured=true', text)
        self.assertEqual(self.lib.get_pmu_committed(), 1)

    def test_failed_csram_qualification_does_not_attempt_pmu(self):
        self.lib.set_fault(0, 0)
        self.lib.capture_menu()
        self.assertEqual(self.lib.get_pmu_calls(), 0)

    def test_emitter_exact_bound_overflow_and_refused_publish_do_not_touch_zone(self):
        before = self.zone()
        self.lib.emit_bytes(2021)
        self.assertEqual((self.lib.used(), self.lib.complete()), (2036, 1))
        self.assertTrue(self.lib.publish())
        self.assertEqual(self.zone()[2048:], before[2048:])
        self.lib.reset()
        before = self.zone()
        self.lib.emit_bytes(2022)
        self.assertEqual((self.lib.used(), self.lib.complete()), (15, 0))
        self.assertFalse(self.lib.publish())
        self.assertEqual(self.zone(), before)
        self.lib.set_position(2037)
        self.assertFalse(self.lib.publish())
        self.assertEqual(self.zone(), before)

    def test_actual_AT_qualifier_fault_nonidentity_and_allowlist_prevent_reads(self):
        for slot, mode in ((0, 0), (108, 0), (109, 1), (109, 2)):
            self.lib.reset()
            self.lib.set_fault(slot, mode)
            snapshot = Snapshot()
            self.assertEqual(self.lib.capture_only(C.byref(snapshot)), 2)
            self.assertEqual(self.lib.get_reads(), 0)
            self.assertEqual(snapshot.read, 0)
        self.lib.reset()
        for address in (0, 0x11bc00, 0x11bc11, 0x11d000):
            self.assertFalse(self.lib.qualify_address(address))
        self.assertEqual(self.lib.get_quals(), 0)

    def test_failed_readback_invalidates_outer_commit(self):
        before = self.zone()
        self.lib.emit_bytes(32)
        self.lib.inject_corruption()
        self.assertFalse(self.lib.publish())
        after = self.zone()
        self.assertEqual(after[:4], bytes(4), 'failed readback left DBGC committed')
        self.assertEqual(after[2048:], before[2048:])


if __name__ == '__main__':
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(Integration)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    (HERE / 'out').mkdir(exist_ok=True)
    (HERE / 'out/integration-tests.json').write_text(json.dumps(dict(
        passed=result.wasSuccessful(), tests=result.testsRun,
        implementation='actual firmware CpuDvfsSnapshot.h and copied capture/formatter headers, compiled with UBSan',
        modeled='AT translation, controller reads, PMU raw return/status, retained RAM, UI and watchdog; assembly separately tested',
        phone_operations=False, shared_firmware_modified=False), indent=2) + '\n')
    raise SystemExit(not result.wasSuccessful())
