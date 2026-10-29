"""Instruction-level probe/caller tests; no emulation of the phone firmware."""
from pathlib import Path
import struct,sys,unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'artifacts/degas/cpu-audit/test-deps'))
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE
import unicorn.arm64_const as R
OUT=ROOT/'artifacts/degas/cpu-context-probe-v3'
CODE=(OUT/'code.bin').read_bytes()
symbols={line.split()[2]:int(line.split()[0],16)-0x10000 for line in (OUT/'symbols.txt').read_text().splitlines()}
def reg(i):return getattr(R,'UC_ARM64_REG_X'+str(i))

class ContextCodeTest(unittest.TestCase):
 def test_ap_preserves_original_register_observation(self):
  for cpu in range(1,8):
   for context in [0,0x13570000|cpu,0x1122334455667788]:
    with self.subTest(cpu=cpu,context=hex(context)):
     u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);base=0x43000000;mail=0x44000000
     u.mem_map(base,4096);u.mem_map(mail,4096);u.mem_write(base,CODE)
     u.mem_write(base+symbols['DegasApProbeMailbox'],struct.pack('<Q',mail))
     inputs=[context]+[0x98760000+i for i in range(1,8)]
     for i,x in enumerate(inputs):u.reg_write(reg(i),x)
     mrs={0xd53800a1:0x81000000|(cpu<<8),0xd5380001:0x411fd461 if cpu<4 else 0x411fd4d0,0xd5384241:4,0xd53be001:13000000}
     off=[False]
     def hook(uc,pc,size,data):
      ins=struct.unpack('<I',uc.mem_read(pc,4))[0]
      if ins in mrs:uc.reg_write(reg(1),mrs[ins]);uc.reg_write(R.UC_ARM64_REG_PC,pc+4)
      elif ins==0xd4000003:
       self.assertEqual(uc.reg_read(reg(0)),0x84000002);off[0]=True;uc.emu_stop()
     u.hook_add(UC_HOOK_CODE,hook);u.emu_start(base,base+4096,count=100)
     self.assertTrue(off[0]);record=struct.unpack('<20Q',u.mem_read(mail,160))
     self.assertEqual(record[0],1);self.assertEqual(record[1],0x81000000|(cpu<<8));self.assertEqual(record[4],13000000)
     self.assertEqual(list(record[6:14]),inputs)
 def test_independent_smc_caller_registers_and_abi(self):
  for cpu in range(1,8):
   u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);base=0x43000000;mail=0x44000000;done=0x45000000
   for p in [base,mail,done]:u.mem_map(p,4096)
   u.mem_write(base,CODE);cookie=0x13570000|cpu;entry=base+2048
   for i,x in enumerate([cpu<<8,entry,cookie,mail]):u.reg_write(reg(i),x)
   for i in range(18,30):u.reg_write(reg(i),0xabc00000+i)
   u.reg_write(R.UC_ARM64_REG_LR,done);called=[False]
   def hook(uc,pc,size,data):
    if pc==done:uc.emu_stop();return
    ins=struct.unpack('<I',uc.mem_read(pc,4))[0]
    if ins==0xd4000003:
     self.assertEqual([uc.reg_read(reg(i)) for i in range(4)],[0xc4000003,cpu<<8,entry,cookie])
     self.assertEqual([uc.reg_read(reg(i)) for i in range(4,18)],[0]*14)
     self.assertEqual(struct.unpack('<4Q',uc.mem_read(mail,32)),(0xc4000003,cpu<<8,entry,cookie))
     for i in range(4,18):uc.reg_write(reg(i),0) # Firmware may clobber volatile registers.
     uc.reg_write(reg(0),0);uc.reg_write(R.UC_ARM64_REG_PC,pc+4);called[0]=True
   u.hook_add(UC_HOOK_CODE,hook);u.emu_start(base+symbols['DegasCpuOnDirect'],done+4,count=100)
   self.assertTrue(called[0]);self.assertEqual(u.reg_read(R.UC_ARM64_REG_PC),done)
   self.assertEqual(u.reg_read(reg(0)),0)
   for i in range(18,30):self.assertEqual(u.reg_read(reg(i)),0xabc00000+i)

if __name__=='__main__':unittest.main()
