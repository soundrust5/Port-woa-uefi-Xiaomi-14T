"""Emulate actual runtime-identity observer, including bounded read failures."""
from pathlib import Path
import hashlib, json, struct, sys, unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'artifacts/degas/cpu-audit/test-deps'))
sys.path.insert(0,str(ROOT/'diagnostic/degas-aplog'))
from decode_runtime_identity import classify
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE
from unicorn.arm64_const import *
A=ROOT/'artifacts/degas/cpu-audit'
CODE=(A/'windows-identity.bin').read_bytes()
META=json.loads((A/'windows-identity.json').read_text())
assert hashlib.sha256(CODE).hexdigest()==META['code_sha256']
for name,digest in META['sources'].items():assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==digest
SYMS={p[2]:int(p[0],16) for line in (A/'windows-identity-symbols.txt').read_text().splitlines() if len(p:=line.split())==3}
O=0x44100000; B=0x43001000; R=0x44000000; RET=0x43000000
BASE=0xfffff80040000000; PC=BASE+0xa91ba0; TCR=0x0695b5513511
RSDS=bytes.fromhex('52534453c06cd400a1e2483e6ac6c224799cfc2301000000')
ENTRY=bytes.fromhex('7f2303d5f353bda9')
class RuntimeIdentity(unittest.TestCase):
 def run_case(self,case):
  u=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
  u.mem_map(O,4096);u.mem_map(B-4096,8192);u.mem_map(R,0x60000)
  code=bytearray(CODE);struct.pack_into('<Q',code,SYMS['DegasWindowsObserverRoot'],R+0x50000);u.mem_write(O,bytes(code))
  def q(a,v):u.mem_write(a,struct.pack('<Q',v))
  q(R+((BASE>>39)&255)*8,R+0x1000|3)
  q(R+0x1000+((BASE>>30)&511)*8,R+0x2000|3)
  q(R+0x2000+((BASE>>21)&511)*8,R+0x3000|3)
  q(R+0x2000+((PC>>21)&511)*8,R+0x4000|3)
  q(R+0x3000+((BASE>>12)&511)*8,R+0x10000|3)
  q(R+0x3000+(((BASE+0x4dc78)>>12)&511)*8,R+0x11000|3)
  q(R+0x4000+((PC>>12)&511)*8,R+0x12000|3)
  u.mem_write(R+0x10000,bytes.fromhex('4d5a900003000000'))
  u.mem_write(R+0x10110,bytes.fromhex('5045000064aa2200'))
  u.mem_write(R+0x11c78,RSDS);u.mem_write(R+0x12ba0,ENTRY)
  if case=='unmapped_rsds':q(R+0x3000+(((BASE+0x4dc78)>>12)&511)*8,0)
  if case=='different_build':u.mem_write(R+0x11c7c,b'\x12\x34\x56\x78')
  if case=='bad_dos':q(R+0x10000,0)
  q(B+0xf0,R);q(B+0xe0,0 if case=='wrong_tcr' else TCR);q(B+0x468,PC)
  initial=dict(sctlr=0x30d00800,ttbr0=0x12345000,ttbr1=0x23456000,tcr=0xabcdefff,mair=0xaabbccdd)
  state=dict(initial);last_read=None;cleaned=[];returned=False
  def reg(n):return globals()['UC_ARM64_REG_X'+str(n)]
  preserved=[reg(i) for i in range(18,31)]+[UC_ARM64_REG_SP]
  for i,r in enumerate(preserved):u.reg_write(r,0xabcdef0000000000+i*0x1000)
  u.reg_write(UC_ARM64_REG_X30,RET);before=[u.reg_read(r) for r in preserved]
  u.reg_write(UC_ARM64_REG_X0,B);u.reg_write(UC_ARM64_REG_X1,0x15eaf7010);u.reg_write(UC_ARM64_REG_X10,0x100)
  def hook(uc,pc,size,data):
   nonlocal returned,last_read
   if pc==RET:returned=True;uc.emu_stop();return
   op=int.from_bytes(uc.mem_read(pc,4),'little');rn=op&31;masked=op&~31
   mrs={0xd5381000:'sctlr',0xd5384240:'el',0xd5382000:'ttbr0',0xd5382020:'ttbr1',0xd5382040:'tcr',0xd538a200:'mair'}
   msr={0xd5181000:'sctlr',0xd5182000:'ttbr0',0xd5182020:'ttbr1',0xd5182040:'tcr',0xd518a200:'mair'}
   if masked in mrs:
    name=mrs[masked];uc.reg_write(reg(rn),(8 if case=='wrong_el' else 4) if name=='el' else state[name]);uc.reg_write(UC_ARM64_REG_PC,pc+4);return
   if masked in msr:
    state[msr[masked]]=0 if rn==31 else uc.reg_read(reg(rn));uc.reg_write(UC_ARM64_REG_PC,pc+4);return
   if op==0xd508871f or masked==0xd50b7e20:
    if masked==0xd50b7e20:
     self.assertEqual(state['sctlr']&5,5);self.assertEqual(uc.reg_read(reg(rn)),last_read);cleaned.append(last_read);last_read=None
    uc.reg_write(UC_ARM64_REG_PC,pc+4)
  def read(uc,access,address,size,value,data):
   nonlocal last_read
   if R<=address<R+0x50000:self.assertEqual(state['sctlr']&5,5);self.assertEqual(size,8);last_read=address
  def write(uc,access,address,size,value,data):
   self.assertTrue(B+0xe00<=address and address+size<=B+0x1000,'write outside private mailbox')
  u.hook_add(UC_HOOK_CODE,hook);u.hook_add(UC_HOOK_MEM_READ,read);u.hook_add(UC_HOOK_MEM_WRITE,write)
  memory=bytes(u.mem_read(R,0x60000));u.emu_start(O,RET+4,count=20000)
  self.assertTrue(returned);self.assertEqual(state,initial)
  self.assertEqual([u.reg_read(r) for r in preserved],before)
  self.assertEqual([u.reg_read(reg(i)) for i in (0,1,10)],[B,0x15eaf7010,0x100])
  self.assertEqual(bytes(u.mem_read(R,0x60000)),memory)
  snapshot=list(struct.unpack('<8Q',u.mem_read(B+0xe00,64)))
  status={'unmapped_rsds':(4<<32)|3,'wrong_tcr':(1<<32)|6,'wrong_el':7}.get(case,0)
  self.assertEqual(snapshot[0],status)
  if not status:
   self.assertEqual(snapshot[1],PC);self.assertEqual(snapshot[2],int.from_bytes(ENTRY,'little'))
   self.assertEqual(snapshot[3],0 if case=='bad_dos' else 0x300905a4d)
   got=struct.pack('<3Q',*snapshot[5:8]);self.assertEqual(got==RSDS,case!='different_build')
   self.assertEqual(len(cleaned),30)
  result=classify({f'mailbox_{0xe00+i*8:03x}':hex(v) for i,v in enumerate(snapshot)})
  self.assertEqual(result['valid_capture'],not bool(status))
  self.assertEqual(result['runtime_build_match'],None if status else case=='success')
 def test_capture_and_guarded_failures(self):
  for case in ['success','different_build','bad_dos','unmapped_rsds','wrong_tcr','wrong_el']:
   with self.subTest(case=case):self.run_case(case)
if __name__=='__main__':unittest.main()
