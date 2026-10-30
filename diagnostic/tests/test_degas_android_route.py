"""Actual linked route bytes: routing guards, entry ABI and full overlapping copy.
Unicorn does not model hardware cache invalidation or reset retention.
"""
from pathlib import Path
import gzip,hashlib,json,struct,sys,unittest,os
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
sys.path.insert(0,str(ROOT/'artifacts/degas/cpu-audit/test-deps'))
from package_degas_dualboot import compose,MAGIC
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm64_const import *
A=ROOT/os.environ.get('DEGAS_ROUTE_TEST_DIR','artifacts/degas/android-route-trace-test')
CODE=(A/'android-route.bin').read_bytes();META=json.loads((A/'dualboot-manifest.json').read_text())
SYMBOLS={p[2]:int(p[0],16) for line in (A/'android-route.symbols.txt').read_text().splitlines() if len(p:=line.split())==3}
TRAMP_OFFSET=SYMBOLS['DegasAndroidTrampoline'];TAG=0x3154554f52414744
assert hashlib.sha256(CODE).hexdigest()==META['route_sha256']
assert hashlib.sha256((ROOT/'diagnostic/degas_android_route.S').read_bytes()).hexdigest()==META['route_source_sha256']
IMAGE=gzip.decompress((A/'kernel.bin.gz').read_bytes())
STOCK=(ROOT/'artifacts/degas/android-stock-Image').read_bytes()
B=0x40000000;R=B+0x10000;T=0x44470000;F=0x480aef00;N=0x2270000

class AndroidRoute(unittest.TestCase):
 def run_route(self,valid=True,inverse=True,el=4,sctlr=0,backup=False,pos=128,corrupt=False,bad_header=0):
  u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);u.mem_map(B,0x4480000);u.mem_map(F&~4095,4096)
  H=0x480af000;CAP=0x40000-12;u.mem_map(H,0x40000)
  if backup:
   u.mem_write(H,struct.pack('<III',0x43474244,pos,CAP))
   token=struct.pack('<QQ',MAGIC,(MAGIC^0xffffffffffffffff)^(1 if corrupt else 0))
   for i,b in enumerate(token):u.mem_write(H+12+(pos-16+i)%CAP,bytes([b]))
   if bad_header:
    sig,p,s=0x43474244,pos,CAP
    if bad_header==1:sig=0
    if bad_header==2:p=CAP+1
    if bad_header==3:s=CAP+1
    if bad_header==4:s=15;p=15
    if bad_header==5:s=64
    u.mem_write(H,struct.pack('<III',sig,p,s))
  u.mem_write(B,IMAGE);flag=(MAGIC if valid else 0,MAGIC^0xffffffffffffffff if inverse else 0)
  u.mem_write(F,struct.pack('<QQ',*flag))
  prior_match=struct.pack('<8Q',TAG,5,1,2,3,4,5,6)
  u.mem_write(F+0x60,prior_match)
  args=[0x47c80000,0xabcdef,0x123456,0x654321]
  for reg,v in zip([UC_ARM64_REG_X0,UC_ARM64_REG_X1,UC_ARM64_REG_X2,UC_ARM64_REG_X3],args):u.reg_write(reg,v)
  u.reg_write(UC_ARM64_REG_SP,0x507ff000)
  daif=0x280;ended=[];ic=[];route_seen=False
  def hook(uc,pc,size,_):
   nonlocal daif,route_seen
   if pc==R:route_seen=True
   if pc==B+0x40 or (pc==B and route_seen):ended.append(pc);uc.emu_stop();return
   op=struct.unpack('<I',uc.mem_read(pc,4))[0]
   if (op&~31) in [0xd5384240,0xd5381000,0xd53b4220]:
    reg=UC_ARM64_REG_X0+(op&31)
    uc.reg_write(reg,{0xd5384240:el,0xd5381000:sctlr,0xd53b4220:daif}[op&~31]);uc.reg_write(UC_ARM64_REG_PC,pc+4)
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
  backup_ok=backup and not corrupt and not bad_header
  matched=(valid and inverse) or backup_ok
  accepted=matched and el==4 and not(sctlr&((1<<25)|5))
  self.assertEqual(ended,[B if accepted else B+0x40]);self.assertEqual(daif,0x280)
  self.assertEqual(u.reg_read(UC_ARM64_REG_SP),0x507ff000)
  self.assertEqual([u.reg_read(r) for r in [UC_ARM64_REG_X0,UC_ARM64_REG_X1,UC_ARM64_REG_X2,UC_ARM64_REG_X3]],args)
  self.assertEqual(bytes(u.mem_read(F,16)),bytes(16) if matched else struct.pack('<QQ',*flag))
  record=struct.unpack('<6Q',bytes(u.mem_read(F+0x20,48)))
  stage=6 if not matched else 3 if el!=4 else 4 if sctlr&((1<<25)|5) else 5
  self.assertEqual(record,(TAG,stage,*flag,el,sctlr if el==4 else 0))
  self.assertEqual(struct.unpack('<Q',bytes(u.mem_read(F+0x58,8)))[0],2 if backup_ok else 1)
  if matched:self.assertEqual(bytes(u.mem_read(F+0x60,64)),bytes(u.mem_read(F+0x20,64)))
  else:self.assertEqual(bytes(u.mem_read(F+0x60,64)),prior_match)
  if backup_ok:
   self.assertEqual(bytes(u.mem_read(H,12)),struct.pack('<III',0x43474244,pos,CAP))
   self.assertEqual(bytes(u.mem_read(H+12+(pos-16+i)%CAP,1)[0] for i in range(16)),bytes(16))
  if accepted:
   expected=STOCK+bytes(N-len(STOCK));self.assertEqual(hashlib.sha256(bytes(u.mem_read(B,N))).digest(),hashlib.sha256(expected).digest())
   self.assertEqual(bytes(u.mem_read(T,0x48)),CODE[TRAMP_OFFSET:TRAMP_OFFSET+0x48])
   first_ic=next(i for i in range(0,TRAMP_OFFSET,4) if CODE[i:i+4]==struct.pack('<I',0xd508751f))
   self.assertEqual(ic,[R+first_ic,T+0x2c])
   self.assertEqual(bytes(u.mem_read(B+N,T-(B+N))),IMAGE[N:T-B])
   if backup_ok:
    ended.clear();route_seen=False;u.mem_write(B,IMAGE)
    u.ctl_remove_cache(B,B+len(IMAGE)) # Fresh LK load, not stale Unicorn TBs.
    u.emu_start(B,B+0x4480000,count=12000000)
    self.assertEqual(ended,[B+0x40])
    self.assertEqual(struct.unpack('<Q',bytes(u.mem_read(F+0x68,8)))[0],5)
  else:
   self.assertEqual(bytes(u.mem_read(B,len(IMAGE))),IMAGE);self.assertEqual(ic,[])
 def test_full_overlap_copy_and_abi(self):self.run_route()
 def test_inherited_icache_enabled(self):self.run_route(sctlr=0x1000)
 def test_missing_and_torn_flag(self):
  self.run_route(valid=False);self.run_route(inverse=False)
 def test_guard_consumes_and_falls_back(self):
  for el,sctlr in [(8,0),(4,1),(4,4),(4,1<<25)]:
   with self.subTest(el=el,sctlr=sctlr):self.run_route(el=el,sctlr=sctlr)
 def test_console_backup_survives_lost_primary(self):self.run_route(valid=False,inverse=False,backup=True)
 def test_console_backup_wrap_and_alignment(self):
  for pos in [0,1,5,15,16,17,0x40000-12]:
   with self.subTest(pos=pos):self.run_route(valid=False,backup=True,pos=pos)
 def test_corrupt_console_token_rejected(self):self.run_route(valid=False,backup=True,corrupt=True)
 def test_backup_consumed_on_guard_rejection(self):self.run_route(valid=False,backup=True,sctlr=1)
 def test_invalid_ring_header_rejected(self):
  for bad in range(1,6):
   with self.subTest(bad=bad):self.run_route(valid=False,backup=True,bad_header=bad)
 def test_packaging_preserves_layout_and_validates_sources(self):
  original=gzip.decompress((ROOT/'artifacts/degas-cpu-cache-visibility-v1/kernel.bin.gz').read_bytes())
  self.assertEqual(IMAGE[0x200000:0x400000],original[0x200000:0x400000])
  self.assertEqual(IMAGE[8:64],original[8:64]);self.assertEqual(len(IMAGE),0x4480000)
  bad=bytearray(STOCK);bad[-1]^=1
  with self.assertRaises(ValueError):compose(gzip.compress(original),bad,CODE)
  bad=bytearray(original);bad[0x10000]=1
  with self.assertRaises(ValueError):compose(gzip.compress(bad),STOCK,CODE)
if __name__=='__main__':unittest.main()
