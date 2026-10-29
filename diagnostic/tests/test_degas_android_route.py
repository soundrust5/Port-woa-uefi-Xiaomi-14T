"""Actual linked route bytes: routing guards, entry ABI and full overlapping copy.
Unicorn does not model hardware cache invalidation or reset retention.
"""
from pathlib import Path
import gzip,hashlib,json,struct,sys,unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
sys.path.insert(0,str(ROOT/'artifacts/degas/cpu-audit/test-deps'))
from package_degas_dualboot import compose,MAGIC
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm64_const import *
A=ROOT/'artifacts/degas/dualboot-route-test'
CODE=(A/'android-route.bin').read_bytes();META=json.loads((A/'dualboot-manifest.json').read_text())
assert hashlib.sha256(CODE).hexdigest()==META['route_sha256']
assert hashlib.sha256((ROOT/'diagnostic/degas_android_route.S').read_bytes()).hexdigest()==META['route_source_sha256']
IMAGE=gzip.decompress((A/'kernel.bin.gz').read_bytes())
STOCK=(ROOT/'artifacts/degas/android-stock-Image').read_bytes()
B=0x40000000;R=B+0x10000;T=0x44470000;F=0x480aef00;N=0x2270000

class AndroidRoute(unittest.TestCase):
 def run_route(self,valid=True,inverse=True,el=4,sctlr=0):
  u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);u.mem_map(B,0x4480000);u.mem_map(F&~4095,4096)
  u.mem_write(B,IMAGE);flag=(MAGIC if valid else 0,MAGIC^0xffffffffffffffff if inverse else 0)
  u.mem_write(F,struct.pack('<QQ',*flag))
  args=[0x47c80000,0xabcdef,0x123456,0x654321]
  for reg,v in zip([UC_ARM64_REG_X0,UC_ARM64_REG_X1,UC_ARM64_REG_X2,UC_ARM64_REG_X3],args):u.reg_write(reg,v)
  u.reg_write(UC_ARM64_REG_SP,0x507ff000)
  daif=0x280;ended=[];ic=[];route_seen=False
  def hook(uc,pc,size,_):
   nonlocal daif,route_seen
   if pc==R:route_seen=True
   if pc==B+0x40 or (pc==B and route_seen):ended.append(pc);uc.emu_stop();return
   op=struct.unpack('<I',uc.mem_read(pc,4))[0]
   if op in [0xd5384245,0xd5381005,0xd53b4231]:
    reg=UC_ARM64_REG_X17 if op==0xd53b4231 else UC_ARM64_REG_X5
    uc.reg_write(reg,{0xd5384245:el,0xd5381005:sctlr,0xd53b4231:daif}[op]);uc.reg_write(UC_ARM64_REG_PC,pc+4)
   elif op in [0xd5034fdf,0xd51b4231,0xd508751f]:
    if op==0xd5034fdf:daif=0x3c0
    elif op==0xd51b4231:daif=uc.reg_read(UC_ARM64_REG_X17)
    else:ic.append(pc)
    uc.reg_write(UC_ARM64_REG_PC,pc+4)
  u.hook_add(UC_HOOK_CODE,hook,begin=B,end=B+0x40)
  u.hook_add(UC_HOOK_CODE,hook,begin=R,end=R+len(CODE)-1)
  # Do not instrument the 36 MiB copy loop; execute the actual instructions.
  u.hook_add(UC_HOOK_CODE,hook,begin=T+0x28,end=T+0x47)
  u.emu_start(B,B+0x4480000,count=12000000)
  accepted=valid and inverse and el==4 and not(sctlr&((1<<25)|5))
  self.assertEqual(ended,[B if accepted else B+0x40]);self.assertEqual(daif,0x280)
  self.assertEqual(u.reg_read(UC_ARM64_REG_SP),0x507ff000)
  self.assertEqual([u.reg_read(r) for r in [UC_ARM64_REG_X0,UC_ARM64_REG_X1,UC_ARM64_REG_X2,UC_ARM64_REG_X3]],args)
  self.assertEqual(bytes(u.mem_read(F,16)),bytes(16) if valid and inverse else struct.pack('<QQ',*flag))
  if accepted:
   expected=STOCK+bytes(N-len(STOCK));self.assertEqual(hashlib.sha256(bytes(u.mem_read(B,N))).digest(),hashlib.sha256(expected).digest())
   self.assertEqual(bytes(u.mem_read(T,0x48)),CODE[0xa0:0xe8]);self.assertEqual(ic,[R+0x84,T+0x2c])
   self.assertEqual(bytes(u.mem_read(B+N,T-(B+N))),IMAGE[N:T-B])
  else:
   self.assertEqual(bytes(u.mem_read(B,len(IMAGE))),IMAGE);self.assertEqual(ic,[])
 def test_full_overlap_copy_and_abi(self):self.run_route()
 def test_inherited_icache_enabled(self):self.run_route(sctlr=0x1000)
 def test_missing_and_torn_flag(self):
  self.run_route(valid=False);self.run_route(inverse=False)
 def test_guard_consumes_and_falls_back(self):
  for el,sctlr in [(8,0),(4,1),(4,4),(4,1<<25)]:
   with self.subTest(el=el,sctlr=sctlr):self.run_route(el=el,sctlr=sctlr)
 def test_packaging_preserves_layout_and_validates_sources(self):
  original=gzip.decompress((ROOT/'artifacts/degas-cpu-cache-visibility-v1/kernel.bin.gz').read_bytes())
  self.assertEqual(IMAGE[0x200000:0x400000],original[0x200000:0x400000])
  self.assertEqual(IMAGE[8:64],original[8:64]);self.assertEqual(len(IMAGE),0x4480000)
  bad=bytearray(STOCK);bad[-1]^=1
  with self.assertRaises(ValueError):compose(gzip.compress(original),bad,CODE)
  bad=bytearray(original);bad[0x10000]=1
  with self.assertRaises(ValueError):compose(gzip.compress(bad),STOCK,CODE)
if __name__=='__main__':unittest.main()
