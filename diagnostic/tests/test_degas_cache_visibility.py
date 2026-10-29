"""AP protocol test with controlled backing-RAM values; not cache hardware emulation."""
from pathlib import Path
import struct,sys,unittest,hashlib,json
ROOT=Path(__file__).resolve().parents[2];A=ROOT/'artifacts/degas/cpu-audit'
sys.path.insert(0,str(A/'test-deps'))
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE,UC_HOOK_MEM_WRITE
from unicorn.arm64_const import *
CODE=(A/'cache-visibility.bin').read_bytes()
META=json.loads((A/'cache-visibility.json').read_text())
assert hashlib.sha256((ROOT/META['source']).read_bytes()).hexdigest()==META['source_sha256']
assert hashlib.sha256(CODE).hexdigest()==META['code_sha256']
LITERAL=META['literal_offset'];V0=0x1122334455667788;V1=0x8877665544332211
class CacheVisibilityTest(unittest.TestCase):
 def run_case(self,dirty=V0,sctlr=0,el=4,timeout=0,off_return=False,base=0x43000000):
  code=bytearray(CODE);mail=base+0x10000;data=base+0x20000;struct.pack_into('<Q',code,LITERAL,mail)
  u=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
  for pa in [base,mail,data]:u.mem_map(pa,4096)
  u.mem_write(base,bytes(code));u.mem_write(mail+0x40,struct.pack('<Q',data));u.mem_write(data,struct.pack('<Q',V0)+bytes([0xa5])*4088)
  reads=[];writes=[];off=[];phase=0
  def q(off):return struct.unpack('<Q',u.mem_read(mail+off,8))[0]
  def hook(uc,pc,size,user):
   nonlocal phase
   op=struct.unpack('<I',uc.mem_read(pc,4))[0]
   if op in [0xd53800a1,0xd5381001,0xd5384241]:
    uc.reg_write(UC_ARM64_REG_X1,{0xd53800a1:0x81000100,0xd5381001:sctlr,0xd5384241:el}[op]);uc.reg_write(UC_ARM64_REG_PC,pc+4);return
   if op==0xd2a02016: # Exercise bounded timeout path using a short test counter.
    uc.reg_write(UC_ARM64_REG_X22,64);uc.reg_write(UC_ARM64_REG_PC,pc+4);return
   if op==0xf9400282:reads.append(q(0))
   if q(0x10)==1:
    done=q(8)
    desired=done+1
    if desired<=3 and desired!=timeout and desired>phase:
     phase=desired;uc.mem_write(data,struct.pack('<Q',V0 if phase==1 else dirty if phase==2 else V1));uc.mem_write(mail,struct.pack('<Q',phase))
   if op==0xd4000003:
    off.append(uc.reg_read(UC_ARM64_REG_X0));self.assertEqual(off[-1],0x84000002)
    if off_return:uc.reg_write(UC_ARM64_REG_X0,0xffffffffffffffff);uc.reg_write(UC_ARM64_REG_PC,pc+4)
    else:uc.emu_stop()
   if op==0xd503205f:uc.emu_stop()
  def write(uc,access,pa,size,value,user):
   self.assertTrue(mail<=pa and pa+size<=mail+4096,'AP wrote outside command/result mailbox');writes.append(pa-mail)
  u.hook_add(UC_HOOK_CODE,hook);u.hook_add(UC_HOOK_MEM_WRITE,write);u.emu_start(base,base+len(CODE),count=10000)
  self.assertEqual(len(off),1);self.assertEqual(q(0x18),0x81000100);self.assertEqual(q(0x20),sctlr)
  expected_status=4 if off_return else 1 if sctlr&0x1005 else 2 if el!=4 else 3 if timeout else 0
  self.assertEqual(q(0x48),expected_status);self.assertEqual(q(0x38),1)
  self.assertEqual(set(writes)-{8,0x10,0x18,0x20,0x28,0x30,0x38,0x48,0x80,0x88,0x90},set())
  if not sctlr&0x1005 and el==4:
   expected=[V0,dirty,V1][:timeout-1 if timeout else 3]
   self.assertEqual(reads,list(range(1,len(expected)+1)))
   self.assertEqual(list(struct.unpack('<3Q',u.mem_read(mail+0x80,24)))[:len(expected)],expected)
  else:self.assertEqual(reads,[])
  if off_return:self.assertEqual(q(0x30),0xffffffffffffffff)
  self.assertEqual(bytes(u.mem_read(data+8,4088)),bytes([0xa5])*4088)
 def test_backing_ram_visibility_scenarios(self):
  for base in [0x43000000,0x340000000]:
   for dirty in [V0,V1,0x123456789abcdef0]:
    with self.subTest(base=hex(base),dirty=hex(dirty)):self.run_case(dirty=dirty,base=base)
 def test_guards_timeouts_and_off_failure(self):
  for sctlr in [1,4,0x1000,0x1005]:self.run_case(sctlr=sctlr)
  self.run_case(el=8)
  for phase in [1,2,3]:self.run_case(timeout=phase)
  self.run_case(off_return=True)
 def test_dirty_window_has_no_explicit_clean_or_logging(self):
  s=(ROOT/'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/CpuCacheVisibility.h').read_text()
  window=s.split('*Data=Dirty;')[1].split('// Same dirty value')[0]
  for forbidden in ['DEBUG','ArmCallSmc','CacheRange','CpuAffinityState']:self.assertNotIn(forbidden,window)
if __name__=='__main__':unittest.main()
