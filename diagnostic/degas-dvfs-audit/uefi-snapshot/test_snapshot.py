"""Run on WSL/Linux: actual C capture/formatter under UBSan, mocked hardware only."""
import ctypes as C
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from decode_snapshot import address_for_slot, parse_journal, decode_frame

HERE = Path(__file__).resolve().parent
class Snapshot(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in ("stage", "sequence", "status", "failed", "qualified", "read")]
    _fields_ += [("valid", C.c_uint32 * 4), ("raw", C.c_uint32 * 109)]

UEFI = r'''
#include <stdint.h>
#include <stddef.h>
typedef uint32_t UINT32; typedef unsigned long long UINT64;
typedef size_t UINTN; typedef int BOOLEAN; typedef void VOID; typedef char CHAR8;
#define STATIC static
#define CONST const
#define TRUE 1
#define FALSE 0
#define EFIAPI
'''
PRINT = r'''
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
static UINTN AsciiSPrint(CHAR8 *out,UINTN n,CONST CHAR8 *format,...){
 char f[384];size_t i=0,j=0;va_list ap;int result;
 while(format[i]){if(format[i]=='L' && format[i+1]=='x'){f[j++]='l';f[j++]='l';i++;}else f[j++]=format[i++];}
 f[j]=0;va_start(ap,format);result=vsnprintf(out,n,f,ap);va_end(ap);
 return result<0?0:(UINTN)result;
}
'''
WRAPPER = r'''
#include "UefiCsramJournal.h"
static UINT32 words[109],qfail,rfail,qcalls,rcalls,bad;
static UINT64 qa[109],ra[109];
static char journal[8192];static size_t used,lines,maxline;
static BOOLEAN qualify(VOID *ctx,UINT64 a){
 UINT32 i=qcalls++;(void)ctx;qa[i]=a;
 if(i>=109 || !DegasDvfsAddressAllowed(a) || a!=DegasDvfsAddressForSlot(i))bad++;
 return i!=qfail;
}
static BOOLEAN readword(VOID *ctx,UINT64 a,UINT32 *out){
 UINT32 i=rcalls++;(void)ctx;ra[i]=a;
 if(qcalls!=109 || i>=109 || !DegasDvfsAddressAllowed(a) || a!=DegasDvfsAddressForSlot(i))bad++;
 *out=i==rfail?0xdeadbeefU:words[i];return i!=rfail;
}
UINT32 host_run(CONST UINT32 *input,UINT32 q,UINT32 r,UINT32 stage,UINT32 seq,DEGAS_DVFS_SNAPSHOT *out){
 memcpy(words,input,sizeof(words));qfail=q;rfail=r;qcalls=rcalls=bad=0;
 memset(qa,0,sizeof(qa));memset(ra,0,sizeof(ra));memset(out,0xa5,sizeof(*out));
 return DegasDvfsCaptureUefi(qualify,readword,NULL,stage,seq,out);
}
UINT32 host_bad(UINT32 kind,DEGAS_DVFS_SNAPSHOT *out){
 qcalls=rcalls=bad=0;
 return DegasDvfsCaptureUefi(kind==1?NULL:qualify,kind==2?NULL:readword,NULL,1,0,kind==3?NULL:out);
}
UINT64 host_address(UINT32 i){return DegasDvfsAddressForSlot(i);}
UINT32 host_allowed(UINT64 a){return DegasDvfsAddressAllowed(a);}
UINT32 host_qcalls(void){return qcalls;}UINT32 host_rcalls(void){return rcalls;}
UINT32 host_bad_calls(void){return bad;}UINT32 host_size(void){return sizeof(DEGAS_DVFS_SNAPSHOT);}
static VOID emit(VOID *ctx,CONST CHAR8 *s){
 size_t n=strlen(s);(void)ctx;if(n>maxline)maxline=n;
 if(used+n>=sizeof(journal)){bad++;return;}memcpy(journal+used,s,n+1);used+=n;lines++;
}
CONST CHAR8 *host_journal(CONST DEGAS_DVFS_SNAPSHOT *s){
 used=lines=maxline=0;journal[0]=0;DegasDvfsJournalUefi(s,emit,NULL);return journal;
}
UINT32 host_lines(void){return (UINT32)lines;}UINT32 host_maxline(void){return (UINT32)maxline;}
'''


def fake_words(mode=0):
    words = [mode]
    for freqs in ([2200, 2000, 1400, 480], [3200, 3000, 2300, 1200, 400], [3350, 3200, 2400, 800, 400]):
        words += [0xABCD0000 | f for f in (freqs + [freqs[-1]] * (32 - len(freqs)))]
        words += [1, 2, 3, 5]
    return words


class Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="degas-dvfs-host-")
        temp = Path(cls.tmp.name)
        (temp / "Library").mkdir()
        (temp / "Uefi.h").write_text(UEFI)
        (temp / "Library/PrintLib.h").write_text(PRINT)
        (temp / "wrapper.c").write_text(WRAPPER)
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
                        "-fsanitize=undefined", "-fno-sanitize-recover=undefined", "-I", str(temp),
                        "-I", str(HERE), str(temp / "wrapper.c"), "-o", str(temp / "core.so")], check=True)
        cls.lib = C.CDLL(str(temp / "core.so"))
        cls.lib.host_run.argtypes = [C.POINTER(C.c_uint32), C.c_uint32, C.c_uint32, C.c_uint32, C.c_uint32, C.POINTER(Snapshot)]
        cls.lib.host_bad.argtypes = [C.c_uint32, C.POINTER(Snapshot)]
        cls.lib.host_address.argtypes = [C.c_uint32]
        cls.lib.host_address.restype = C.c_uint64
        cls.lib.host_allowed.argtypes = [C.c_uint64]
        cls.lib.host_journal.argtypes = [C.POINTER(Snapshot)]
        cls.lib.host_journal.restype = C.c_char_p
        cls.cases = 0

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_capture(self, words=None, qfail=109, rfail=109, stage=3, seq=0x1234ABCD):
        words = fake_words() if words is None else words
        s = Snapshot()
        status = self.lib.host_run((C.c_uint32 * 109)(*words), qfail, rfail, stage, seq, C.byref(s))
        self.assertEqual(status, s.status)
        self.assertEqual(self.lib.host_bad_calls(), 0)
        type(self).cases += 1
        return s

    def frame(self, s):
        text = self.lib.host_journal(C.byref(s)).decode()
        self.assertEqual(self.lib.host_lines(), 18)
        self.assertLess(self.lib.host_maxline(), 192)
        self.assertLess(len(text), 1800)
        return parse_journal(text)[0], text

    def test_exact_allowlist_and_page_boundaries(self):
        expected = {0x11C0CC}
        for base in (0x11BC10, 0x11BD30, 0x11BE50):
            expected.update(base + i * 4 for i in range(32))
            expected.update(base + n for n in (0x84, 0x88, 0x8C, 0x114))
        self.assertEqual(len(expected), 109)
        self.assertEqual({self.lib.host_address(i) for i in range(109)}, expected)
        for i in range(109):
            self.assertEqual(self.lib.host_address(i), address_for_slot(i))
        for address in list(range(0x11B000, 0x11D000)) + [0, 0x11BBFF, 0x11D000, 0xFFFFFFFFFFFFFFFF]:
            self.assertEqual(bool(self.lib.host_allowed(address)), address in expected, hex(address))
        for slot in (109, 110, 0xFFFFFFFF):
            self.assertEqual(self.lib.host_address(slot), 0)

    def test_complete_capture_and_real_formatter(self):
        self.assertEqual(self.lib.host_size(), C.sizeof(Snapshot))
        s = self.run_capture()
        self.assertEqual((s.status, s.qualified, s.read, s.failed), (0, 109, 109, 109))
        self.assertEqual(list(s.raw), fake_words())
        frame, text = self.frame(s)
        report = decode_frame(frame)
        self.assertEqual(report["source"], "UEFI read")
        self.assertEqual(report["stage"], "UEFI before OS loader")
        self.assertFalse(report["physical_clock_measured"])
        self.assertFalse(report["windows_runtime_observation"])
        self.assertEqual(report["domains"][0]["requested_candidate_khz"], 1400000)
        out = HERE / "out"
        out.mkdir(exist_ok=True)
        (out / "synthetic-only-example.log").write_text(text)
        (out / "synthetic-only-example.decoded.json").write_text(json.dumps(report, indent=2) + "\n")

    def test_every_qualification_failure_prevents_all_reads(self):
        for fail in range(109):
            s = self.run_capture(qfail=fail)
            self.assertEqual((s.status, s.qualified, s.read, s.failed), (2, fail, 0, fail))
            self.assertEqual(self.lib.host_qcalls(), fail + 1)
            self.assertEqual(self.lib.host_rcalls(), 0)
            f, _ = self.frame(s)
            self.assertEqual(decode_frame(f)["successful_reads"], 0)

    def test_every_read_failure_stops_and_does_not_publish_poison(self):
        for fail in range(109):
            s = self.run_capture(rfail=fail)
            self.assertEqual((s.status, s.qualified, s.read, s.failed), (3, 109, fail, fail))
            self.assertEqual(self.lib.host_rcalls(), fail + 1)
            self.assertEqual(list(s.raw)[fail:], [0] * (109 - fail))
            f, _ = self.frame(s)
            self.assertEqual(sum(f["valid"]), fail)
            self.assertEqual(decode_frame(f)["capture_status"], "read failed")

    def test_arguments_and_stages_do_no_reads(self):
        s = Snapshot()
        for kind in (1, 2, 3):
            self.assertEqual(self.lib.host_bad(kind, C.byref(s)), 1)
            self.assertEqual((self.lib.host_qcalls(), self.lib.host_rcalls()), (0, 0))
        for stage in (0, 5, 0xFFFFFFFF):
            s = self.run_capture(stage=stage)
            self.assertEqual((s.status, s.qualified, s.read, s.failed), (4, 0, 0, 109))
            self.frame(s)
        for stage in range(1, 5):
            self.assertEqual(self.run_capture(stage=stage).status, 0)

    def test_units_and_request_are_not_clock_measurements(self):
        words = fake_words(mode=1)
        words[34] = 1400000
        words[70] = 2300000
        words[106] = 3350000
        report = decode_frame(self.frame(self.run_capture(words))[0])
        for domain, mhz in zip(report["domains"], (1400, 2300, 3350)):
            self.assertEqual(domain["requested_candidate_mhz"], mhz)
            self.assertEqual(domain["reported_transition_latency_ns"], 5000)
            self.assertTrue(domain["cpufreq_ready_bit0"])
            self.assertTrue(domain["svs_ready_bit1"])
        self.assertEqual(report["domains"][0]["lut_raw_and_encoded_mhz"][0]["encoded_mhz"], 2200)
        self.assertFalse(report["physical_clock_measured"])
        words[34], words[36] = 31, 0
        words[35] = 2
        d = decode_frame(self.frame(self.run_capture(words))[0])["domains"][0]
        self.assertIsNone(d["requested_candidate_khz"])
        self.assertIsNone(d["reported_transition_latency_ns"])
        self.assertFalse(d["cpufreq_ready_bit0"])
        self.assertTrue(d["svs_ready_bit1"])
        words = fake_words(mode=0)
        words[34] = 31
        d = decode_frame(self.frame(self.run_capture(words))[0])["domains"][0]
        self.assertIsNone(d["requested_candidate_khz"])
        words[1] = 0
        self.assertFalse(decode_frame(self.frame(self.run_capture(words))[0])["domains"][0]["lut_qualified_for_request_decode"])

    def test_khz_write_mode_also_decodes_boot_index_readback(self):
        report = decode_frame(self.frame(self.run_capture(fake_words(mode=1)))[0])
        self.assertEqual([d["requested_candidate_khz"] for d in report["domains"]],
                         [1400000, 2300000, 2400000])
        self.assertTrue(all(d["request_interpretation"] == "LUT index request" for d in report["domains"]))

    def test_decoder_rejects_truncation_duplicates_and_source_relabelling(self):
        _, text = self.frame(self.run_capture())
        mutations = [text.rsplit("DVF1 E", 1)[0], text.replace("src=UEFI", "src=Windows"),
                     text.replace("read=109", "read=108"), text.replace("seq=1234abcd\n", "seq=00000000\n"),
                     text + text.splitlines()[1] + "\n", text.replace("mask=fffffffff", "mask=ffffffffe", 1),
                     text.replace("DVF1 E", text.splitlines()[1] + "\nDVF1 E", 1)]
        for mutated in mutations:
            with self.assertRaises(ValueError):
                parse_journal(mutated)
        self.assertEqual(len(parse_journal(text + text)), 2)


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(Tests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    report = {"tests_run": result.testsRun, "passed": result.wasSuccessful(), "actual_C_capture_cases": Tests.cases,
              "hardware_access": False, "compiler_flags": "-Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=undefined",
              "source_sha256": {n: hashlib.sha256((HERE / n).read_bytes()).hexdigest() for n in
                                ("UefiCsramSnapshot.h", "UefiCsramJournal.h", "decode_snapshot.py", "test_snapshot.py")}}
    (HERE / "out").mkdir(exist_ok=True)
    (HERE / "out/host-tests.json").write_text(json.dumps(report, indent=2) + "\n")
    raise SystemExit(not result.wasSuccessful())
