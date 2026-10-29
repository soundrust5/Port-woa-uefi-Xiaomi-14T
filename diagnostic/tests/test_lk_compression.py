"""Execute the exact pinned LK compression dispatch, not its decompressor."""
import faulthandler
import hashlib
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/vendor/python'))
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm64_const import UC_ARM64_REG_X22, UC_ARM64_REG_X24

class DispatchTest(unittest.TestCase):
    def test_exact_dispatch(self):
        raw = (ROOT/'evidence/extracted/2026-09-14/lk-analysis/lk-payload.bin').read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),'6afd84aefda089681f0ec4a224c6710228f525ec312bfa2532977be9e4496580')
        cases = [('gzip',b'\x1f\x8b\x08\0','gzip'),('legacy-lz4',b'\x02\x21\x4c\x18','legacy-lz4'),
                 ('modern-lz4',b'\x04\x22\x4d\x18','reject'),('zero',bytes(4),'reject'),
                 ('probe-raw',(ROOT/'diagnostic/build/entry.bin').read_bytes()[:4],'reject'),
                 ('probe-gzip',(ROOT/'diagnostic/build/entry.bin.gz').read_bytes()[:4],'gzip')]
        results=[]
        for name, header, expected in cases:
            with self.subTest(name=name):
                uc=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
                # Synthetic addresses only; no hardware placement claim.
                enabled=faulthandler.is_enabled()
                if enabled: faulthandler.disable()
                try:
                    uc.mem_map(0x100000,0x110000)
                    uc.mem_map(0x300000,0x1000)
                finally:
                    if enabled: faulthandler.enable()
                uc.mem_write(0x100000,raw)
                uc.mem_write(0x300000,header)
                uc.reg_write(UC_ARM64_REG_X22,0x300000)
                uc.reg_write(UC_ARM64_REG_X24,0x184c2102)
                destinations={0x10e9c0:'legacy-lz4',0x10ea4c:'gzip',0x10ec10:'reject'}
                reached=[]; writes=[]
                def hook(engine,address,size,userdata):
                    if address in destinations:
                        reached.append(destinations[address]); engine.emu_stop()
                uc.hook_add(UC_HOOK_CODE,hook)
                uc.hook_add(UC_HOOK_MEM_WRITE,lambda *args:writes.append(args[2]))
                uc.emu_start(0x10e948,0x10ec1c,count=64)
                self.assertEqual(reached,[expected])
                self.assertEqual(writes,[])
                results.append(dict(case=name,classification=expected))
        (ROOT/'evidence/lk-compression-dispatch-tests.json').write_text(json.dumps(dict(cases=results,scope='exact instructions 0xe948 through dispatch; no decompression or SMC executed',phone_tested=False),indent=2)+'\n')

if __name__=='__main__': unittest.main()
