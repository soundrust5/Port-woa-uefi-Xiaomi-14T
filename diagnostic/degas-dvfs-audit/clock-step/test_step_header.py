"""Actual CpuDvfsStep.h with modeled hardware callbacks. Run in WSL/Linux.

No phone access. These tests check normal control flow, not exception recovery
or physical deadline enforcement by the assembly PMU reader.
"""
import ctypes as C
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'uefi-snapshot'))
from test_snapshot import UEFI, PRINT

FIRMWARE = HERE.parents[2] / 'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe'
STUB = r'''
#include <string.h>
#include <stdlib.h>
typedef uint8_t UINT8;
#define DEBUG_ERROR 0
#define DEBUG(x) do{}while(0)
#define ZeroMem(p,n) memset(p,0,n)
#define CopyMem memcpy
#define MemoryFence() do{}while(0)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
typedef struct {UINT32 Status,Raw[109];} DEGAS_DVFS_SNAPSHOT;
static UINT32 fault, calls, count, request, enabled, pubs,restore_reads;
static UINT32 values[16]; static UINT64 addresses[16],clock_ticks;
static char journal[2048];
static UINT32 mDvfsJournal[512];static UINTN mDvfsJournalPosition;static BOOLEAN mDvfsJournalComplete;
static UINT64 ArmReadCurrentEL(void){return fault==1?8:4;}
static UINT64 ArmReadMpidr(void){return fault==2?1:0;}
static UINT64 DegasNativeTranslate(UINT64 a,UINT32 w){
 if(w!=1)abort();return fault==3?1:a&~4095ULL;
}
static UINT32 DegasDvfsPmuRead(UINT64*p){
 UINT32 khz=request?request:1300000;++calls;
 if(fault==4 && !count)return 1;
 if(fault==12 && request==2000000)return 1;
 if(fault==7 && request==1400000)khz=1300000;
 if(fault==8 && request==2000000)khz=1400000;
 if(fault==18 && request==2000000)khz=1500000;
 if(fault==18 && count>=5 && request==1300000 && !restore_reads++)khz=1800000;
 if(fault==20 && count>=5 && request==1300000 && !restore_reads++)khz=1800000;
 if(fault==9 && count>=5 && request==1300000)return 1;
 p[0]=0x11f010305619ULL;p[1]=0x3041;p[2]=0x80000000;p[3]=0;
 p[4]=13000000;p[5]=1000;p[6]=1000;p[7]=131000;p[8]=1000+khz*10ULL;
 if(fault==13 && !count && calls==2)p[1]^=8;
 if(fault==14 && !count)p[8]=p[6];
 if(fault==15 && !count)p[7]=p[5];
 if(fault==19 && request==2000000)p[1]^=8;
 clock_ticks+=(fault==16 && request==1400000)?100000000:10000000;
 return 0;
}
static void MmioWrite32(UINTN a,UINT32 v){
 if(count>=16)abort();addresses[count]=a;values[count++]=v;
 if(a==0x11bc94)enabled=v;else if(a==0x11bc98)request=v;else abort();
}
static UINT32 MmioRead32(UINTN a){
 if(a==0x11bc9c)return fault==5?0:3;
 if(a==0x11bc98)return fault==6 && count==2?0:request;
 abort();
}
static void MicroSecondDelay(UINTN us){clock_ticks+=us*1000;}
static UINT64 GetPerformanceCounter(void){return clock_ticks;}
static UINT64 GetTimeInNanoSecond(UINT64 t){return t;}
static void DvfsEmit(VOID*c,CONST CHAR8*s){(void)c;strcat(journal,s);mDvfsJournalPosition+=strlen(s);if(mDvfsJournalPosition>2036)abort();}
static BOOLEAN DvfsPublish(void){++pubs;return !((fault==10 && pubs==1) || (fault==17 && pubs==2));}
#include "CpuDvfsStep.h"
UINT32 run(UINT32 f){
 static const UINT32 lut[]={2200,2100,2000,1900,1800,1700,1600,1500,1400,1300,1200,1100,1000,900,800,700,600,480,480};
 DEGAS_DVFS_SNAPSHOT s={0};UINT32 n;fault=f;calls=count=request=enabled=pubs=restore_reads=0;clock_ticks=0;
 memset(journal,0,sizeof(journal));
 mDvfsJournalPosition=15;
 s.Raw[0]=1;s.Raw[33]=0x101;s.Raw[34]=9;s.Raw[35]=3;
 for(n=0;n<ARRAY_SIZE(lut);n++)s.Raw[n+1]=lut[n];
 if(f==11)s.Raw[34]=8;
 return CpuDvfsClockStep(&s);
}
UINT32 writes(void){return count;}
UINT32 value(UINT32 n){return values[n];}
UINT64 address(UINT32 n){return addresses[n];}
CONST CHAR8* log_text(void){return journal;}
UINT32 trace_count(UINT32 phase){return mClockTraceCount[phase];}
UINT64 trace_word(UINT32 phase,UINT32 index,UINT32 word){return mClockTrace[phase][index].Raw[word];}
UINT32 trace_error(UINT32 phase,UINT32 index){return mClockTrace[phase][index].Error;}
UINT32 summary_used(void){return mDvfsJournalPosition;}
'''


class ActualHeader(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='degas-step-')
        directory = Path(cls.tmp.name)
        (directory / 'actual.c').write_text(UEFI + PRINT.replace("format[i+1]=='x'", "(format[i+1]=='x' || format[i+1]=='u')") + STUB)
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-misleading-indentation', '-shared', '-fPIC',
                        '-fsanitize=undefined', '-fno-sanitize-recover=undefined',
                        '-I', str(FIRMWARE), str(directory / 'actual.c'),
                        '-o', str(directory / 'actual.so')], check=True)
        cls.lib = C.CDLL(str(directory / 'actual.so'))
        cls.lib.address.restype = C.c_uint64
        cls.lib.log_text.restype = C.c_char_p
        cls.lib.trace_word.restype = C.c_uint64

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def writes(self):
        return [(self.lib.address(n), self.lib.value(n)) for n in range(self.lib.writes())]

    def test_success_exact_domain0_protocol(self):
        self.assertEqual(self.lib.run(0), 0)
        self.assertEqual(self.writes(), [(0x11bc94, 1), (0x11bc98, 1300000),
                                        (0x11bc98, 1400000), (0x11bc98, 2000000),
                                        (0x11bc98, 1300000)])

    def test_preconditions_never_write(self):
        for fault in (1, 2, 3, 4, 10, 11, 13, 14, 15):
            with self.subTest(fault=fault):
                self.assertNotEqual(self.lib.run(fault), 0)
                self.assertEqual(self.writes(), [])

    def test_every_handled_post_enable_failure_restores_khz(self):
        for fault in (5, 6, 7, 8, 12, 16):
            with self.subTest(fault=fault):
                self.assertNotEqual(self.lib.run(fault), 0)
                self.assertEqual(self.writes()[-1], (0x11bc98, 1300000))
                self.assertTrue(all(a in (0x11bc94, 0x11bc98) for a, _ in self.writes()))

    def test_preflight_clamp_and_expired_elapsed_gate_prevent_2ghz(self):
        for fault in (7, 16):
            self.lib.run(fault)
            self.assertNotIn((0x11bc98, 2000000), self.writes())

    def test_final_journal_failure_does_not_report_success(self):
        self.assertEqual(self.lib.run(17), 6)
        self.assertEqual(self.writes()[-1], (0x11bc98, 1300000))

    def test_restore_failure_is_explicit(self):
        self.assertEqual(self.lib.run(9), 5)
        self.assertIn(b'status=5', self.lib.log_text())

    def test_persistent_clamp_is_nonzero_and_transient_restore_converges(self):
        self.assertEqual(self.lib.run(18), 4)
        self.assertIn(b'high=1500000 restored=1300000', self.lib.log_text())
        self.assertEqual(self.writes()[-1], (0x11bc98, 1300000))
        self.assertEqual(self.lib.trace_error(4, 0), 7)
        self.assertEqual(self.lib.trace_count(4), 5)

    def test_transient_restore_does_not_fail_successful_step(self):
        self.assertEqual(self.lib.run(20), 0)
        self.assertIn(b'high=2000000 restored=1300000', self.lib.log_text())
        self.assertLess(self.lib.summary_used(), 2036)

    def test_metadata_failure_preserves_raw_sample(self):
        self.assertEqual(self.lib.run(19), 4)
        self.assertEqual(self.lib.trace_error(3, 0), 4)
        self.assertEqual(self.lib.trace_word(3, 0, 1), 0x3049)
        self.assertEqual(self.lib.trace_word(3, 0, 8), 20001000)


if __name__ == '__main__':
    unittest.main()
