"""Protocol/sysreg sequencing tests; cache/MMU behavior is explicitly modeled."""
from pathlib import Path
import hashlib,json,struct,sys,unittest
ROOT=Path(__file__).resolve().parents[2];A=ROOT/'artifacts/degas/cpu-audit'
sys.path.insert(0,str(A/'test-deps'))
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE,UC_HOOK_MEM_WRITE
from unicorn.arm64_const import *
CODE=(A/'coherent-probe.bin').read_bytes();META=json.loads((A/'coherent-probe.json').read_text())
assert hashlib.sha256(CODE).hexdigest()==META['code_sha256']
assert hashlib.sha256((ROOT/META['source']).read_bytes()).hexdigest()==META['source_sha256']
V0=0x1122334455667788;V1=0x8877665544332211
class CoherentProbe(unittest.TestCase):
 def run_case(self,sctlr=0,el=4,timeout=0,off_return=False,base=0x43000000):
  mail=base+0x10000;data=base+0x20000
  code=bytearray(CODE);struct.pack_into('<Q',code,META['literal_offset'],mail)
  u=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
  for pa in [base,mail,data]:u.mem_map(pa,4096)
  u.mem_write(base,bytes(code));u.mem_write(data,struct.pack('<Q',V0)+bytes([0xa5])*4088)
  private=[base+0x30000,0x100803019,0xff0044]
  u.mem_write(mail+0x40,struct.pack('<Q',data));u.mem_write(mail+0x50,struct.pack('<3Q',*private))
  old={'sctlr':sctlr,'ttbr0':0x12340000,'ttbr1':0x23450000,'tcr':0xdeadbeef,'mair':0xcafeface}
  state=dict(old);reads=[];civac=[];transitions=[];off=[]
  mr={0xd53800a0:'mpidr',0xd5381000:'sctlr',0xd5384240:'el',0xd5382000:'ttbr0',0xd5382020:'ttbr1',0xd5382040:'tcr',0xd538a200:'mair'}
  mw={0xd5181000:'sctlr',0xd5182000:'ttbr0',0xd5182020:'ttbr1',0xd5182040:'tcr',0xd518a200:'mair'}
  def reg(n):return UC_ARM64_REG_X0+n if n<29 else UC_ARM64_REG_X29 if n==29 else UC_ARM64_REG_X30
  def q(o):return struct.unpack('<Q',u.mem_read(mail+o,8))[0]
  def hook(uc,pc,size,_):
   op=struct.unpack('<I',uc.mem_read(pc,4))[0];masked=op&~31;rn=op&31
   if masked in mr:
    name=mr[masked];uc.reg_write(reg(rn),0x81000100 if name=='mpidr' else el if name=='el' else state[name]);uc.reg_write(UC_ARM64_REG_PC,pc+4);return
   if masked in mw:
    name=mw[masked];value=0 if rn==31 else uc.reg_read(reg(rn));state[name]=value
    if name=='sctlr':
     transitions.append(value)
     if value&5==5:self.assertEqual([state['ttbr0'],state['tcr'],state['mair']],private);self.assertEqual(state['ttbr1'],0)
    uc.reg_write(UC_ARM64_REG_PC,pc+4);return
   if op in [0xd5034fdf,0xd508871f,0xd50b7e34]:
    if op==0xd50b7e34:
     self.assertEqual(reads,[1,2,3]);self.assertEqual(state['sctlr']&5,5);self.assertEqual(uc.reg_read(UC_ARM64_REG_X20),data);civac.append(data)
    uc.reg_write(UC_ARM64_REG_PC,pc+4);return
   if op==0xd2a02016:uc.reg_write(UC_ARM64_REG_X22,64);uc.reg_write(UC_ARM64_REG_PC,pc+4);return
   if q(0x10)==1:
    desired=q(8)+1
    if desired<=4 and desired!=timeout:u.mem_write(mail,struct.pack('<Q',desired))
   if op==0xf9400282:
    phase=uc.reg_read(UC_ARM64_REG_X21);reads.append(phase)
    self.assertEqual(state['sctlr']&5,5 if phase==3 else sctlr&5)
    u.mem_write(data,struct.pack('<Q',V0 if phase<3 else V1))
   if op==0xd4000003:
    off.append(uc.reg_read(UC_ARM64_REG_X0));self.assertEqual(off[-1],0x84000002);self.assertEqual(state,old)
    if off_return:uc.reg_write(UC_ARM64_REG_X0,0xffffffffffffffff);uc.reg_write(UC_ARM64_REG_PC,pc+4)
    else:uc.emu_stop()
   if op==0xd503205f:uc.emu_stop()
  def write(uc,access,pa,size,value,_):self.assertTrue(mail<=pa and pa+size<=mail+4096,'AP write outside private UC mailbox')
  u.hook_add(UC_HOOK_CODE,hook);u.hook_add(UC_HOOK_MEM_WRITE,write);u.emu_start(base,base+len(CODE),count=10000)
  self.assertEqual(off,[0x84000002]);self.assertEqual(q(0x38),1)
  bad=sctlr&0x1005 or sctlr&(1<<25)
  status=4 if off_return else 1 if bad else 2 if el!=4 else 3 if timeout else 0
  self.assertEqual(q(0x48),status)
  if not bad and el==4:
   n=timeout-1 if timeout else 4
   self.assertEqual(reads,list(range(1,n+1)));self.assertEqual(list(struct.unpack('<4Q',u.mem_read(mail+0x80,32)))[:n],[V0,V0,V1,V1][:n])
   self.assertEqual(civac,[data] if n>=3 else []);self.assertEqual(transitions,[sctlr|5,sctlr] if n>=3 else [])
   self.assertEqual(q(0xb0),4 if n>=3 else 0)
  else:self.assertEqual(reads,[]);self.assertEqual(transitions,[])
  self.assertEqual(bytes(u.mem_read(data+8,4088)),bytes([0xa5])*4088)
 def test_success_low_and_high_physical_addresses(self):
  self.run_case();self.run_case(base=0x140100000)
 def test_bounded_phase_timeouts(self):
  for phase in [1,2,3,4]:self.run_case(timeout=phase)
 def test_reject_unsafe_initial_state(self):
  for s in [1,4,0x1000,1<<25]:self.run_case(sctlr=s)
  self.run_case(el=8)
 def test_cpu_off_return_reported(self):self.run_case(off_return=True)
if __name__=='__main__':unittest.main()
