"""Historical uncached observer fixture (before the separate coherent observer).

This pins the earlier linked bytes and recorded source hashes, not today's
Parking.S. Current integration is test_degas_windows_observer.py.
"""
from pathlib import Path
import hashlib,json,struct,sys,unittest,uuid
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'artifacts/degas/cpu-audit/test-deps'))
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE,UC_HOOK_MEM_WRITE
from unicorn.arm64_const import *
sys.path.insert(0,str(ROOT/'diagnostic/degas-aplog'))
import ap_journal_v1 as journal
A=ROOT/'artifacts/degas/cpu-audit'
CODE=(A/'parking-prcb-code.bin').read_bytes()
META=json.loads((A/'parking-prcb-code.json').read_text())
assert META['sources']=={
 'mu/Platforms/Xiaomi/degasPkg/Drivers/DegasAcpiDxe/Parking.S':'d723a890d342407afc6a6bc42ae1e8cc6d56ee2cffbbaa3717a7dea56d73a4fc',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/DegasAcpiDxe/PrcbWalk.inc':'29149c806f9e0f713c92d00c9fcf3008337013cea3d3cc608083f52916aa366e',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/DegasAcpiDxe/ApJournalCommit.inc':'a3d9e928a08e575346647625efab3d38d24549891ed13660e7beac6721b2f3ac'}
assert META['code_sha256']=='e7b9e5e8a546ed0523d79bcd6731732b1f3abfe62ab95299c386aa1da1f7d69a'
assert hashlib.sha256(CODE).hexdigest()==META['code_sha256']
SYMS={line.split()[2]:int(line.split()[0],16) for line in (A/'parking-prcb-symbols.txt').read_text().splitlines()}
FB=0xfcc2f000;B=0x43000000;R=0x44000000
TCR=0x0695b5513511;LPB=0xfffff80010000000;PRCB=0xfffff80020000980;BSP_PRCB=0xfffff80030000980
HEX=['111101101101111','010110010010111','111001111100111','111001111001111','101101111001001','111100111001111','111100111101111','111001001001001','111101111101111','111101111001111','111101111101101','110101110101110','111100100100111','110101101101110','111100111100111','111100111100100']
class IntegratedParkingTest(unittest.TestCase):
 def run_case(self,case,cpu=1,root_half=False,sctlr=0,current_el=4):
  u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);u.mem_map(B,0x20000);u.mem_map(R,0x40000);u.mem_map(FB,0xcf0000)
  u.mem_map(journal.ZONE_BASE,journal.ZONE_BYTES)
  attempt=uuid.UUID('12345678-1234-5678-9abc-def012345678');build=bytes(range(32))
  zone=journal.make_reference_zone(attempt,build,B-cpu*4096)
  u.mem_write(journal.ZONE_BASE,bytes(zone));journal_writes=[]
  root=R+(0x800 if root_half else 0);ttbr=0x1234000000000001|root;target=B+0x10000
  def q(pa,value):u.mem_write(pa,struct.pack('<Q',value))
  def desc(pa,index,value):q(pa+index*8,value)
  desc(root,(LPB>>39)&255,(R+0x1000)|3)
  desc(R+0x1000,(LPB>>30)&511,(R+0x2000)|3)
  for va,l3,pa,pages in [(LPB,R+0x3000,R+0x10000,1),(PRCB&~4095,R+0x4000,R+0x20000,9),(BSP_PRCB&~4095,R+0x5000,R+0x30000,9)]:
   desc(R+0x2000,(va>>21)&511,l3|3)
   for i in range(pages):desc(l3,((va>>12)&511)+i,(pa+i*4096)|3)
  lpb_prcb=0 if case=='null_prcb' else BSP_PRCB if case in ['lpb_bsp','tpidr_flags'] else PRCB
  q(R+0x10088,lpb_prcb)
  ap_phys=R+0x20980;bsp_phys=R+0x30980
  q(ap_phys+0x20,(1<<32)|0x12345678)
  q(bsp_phys+0x20,0x87654321) # BSP Number0, deliberately distinct queues.
  q(bsp_phys+0x8300,0xfffff80050505050);q(bsp_phys+0x1908,0xfffff80060606060)
  local=0 if case=='zero_queue' else 0xfffff80030000000
  shared=0 if case=='zero_queue' else 0xfffff80040000000
  q(ap_phys+0x8300,local);q(ap_phys+0x1908,shared)
  if case=='invalid_table':desc(root,(LPB>>39)&255,0)
  if case=='invalid_tpidr_l3':desc(R+0x4000,((PRCB+0x20)>>12)&511,0xdead0000)
  if case=='out_of_ram':ttbr=0x112b0000
  if case=='out_of_ram_leaf':desc(R+0x3000,(LPB>>12)&511,0x112b0000|3)
  tcr=TCR if case!='wrong_tcr' else 0
  tpidr=0 if case=='invalid_tpidr' else (PRCB-0x980)|(0xabc if case=='tpidr_flags' else 0)
  if case=='invalid_tpidr_l0':tpidr=0xffffb90093e81000
  if case in ['invalid_tpidr_l1','invalid_tpidr_l2']:tpidr=0xfffff80040000000
  if case=='invalid_tpidr_l2':desc(R+0x1000,(tpidr>>30)&511,(R+0x6000)|3)
  u.mem_write(B+16,b'\xa5'*(2048-16));u.mem_write(B+2048,CODE)
  for off,value in [(0xf0,ttbr),(0xe0,tcr),(0x40,tpidr),(0x368,LPB),(0x468,0xfffff80000a91ba0)]:q(B+off,value)
  canary=bytes(u.mem_read(B+16,2048-16));tables=bytes(u.mem_read(R,0x40000))
  entered=False;released=False;helper_calls=0;helper_regs=None;screen={};all_writes=[]
  private_regs=[globals()['UC_ARM64_REG_X'+str(i)] for i in range(18,31)]+[UC_ARM64_REG_SP]
  def hook(uc,pc,size,data):
   nonlocal entered,released,helper_calls,helper_regs
   if pc==target:entered=True;uc.emu_stop();return
   op=bytes(uc.mem_read(pc,4))
   if op==bytes.fromhex('a10038d5'):uc.reg_write(UC_ARM64_REG_X1,0x81000000|(cpu<<8));uc.reg_write(UC_ARM64_REG_PC,pc+4);return
   if op==bytes.fromhex('021038d5'):uc.reg_write(UC_ARM64_REG_X2,sctlr);uc.reg_write(UC_ARM64_REG_PC,pc+4);return
   if op==bytes.fromhex('434238d5'):uc.reg_write(UC_ARM64_REG_X3,current_el);uc.reg_write(UC_ARM64_REG_PC,pc+4);return
   if pc==B+2048+SYMS['DegasPrcbRead64']:
    helper_calls+=1;helper_regs=[uc.reg_read(r) for r in private_regs]
   helper_start=B+2048+SYMS['DegasPrcbRead64'];helper_end=B+2048+SYMS['DegasPrcbRead64End']
   if helper_start<=pc<helper_end and op==bytes.fromhex('c0035fd6') and not helper_start<=uc.reg_read(UC_ARM64_REG_X30)<helper_end:
    self.assertEqual([uc.reg_read(r) for r in private_regs],helper_regs,'reader destroyed private caller state')
   if not released and struct.unpack('<Q',uc.mem_read(B+0xf00,8))[0]==1:
    q(B+8,target);uc.mem_write(B,struct.pack('<I',cpu<<8));released=True
  def write_hook(uc,access,address,size,value,data):
   all_writes.append((address,size))
   if FB<=address<FB+0xcf0000:
    for part in range(size//4):screen[address+part*4]=(value>>(32*part))&0xffffffff
   elif journal.ZONE_BASE<=address<journal.ZONE_BASE+journal.ZONE_BYTES:
    slot=journal.ZONE_BASE+cpu*journal.SLOT_BYTES
    self.assertTrue((address==slot and size==8) or slot+0x40<=address and address+size<=slot+0xf8,'AP wrote outside its owned journal payload/commit')
    journal_writes.append((address,size))
   else:
    offset=address-B
    self.assertTrue((offset in [0,8] and size in [4,8]) or
      (0xe00<=offset and offset+size<=0xe40) or
      (0xf00<=offset and offset+size<=0xf40),
      f'write outside mailbox handshake/snapshot/telemetry: {address:x}')
  u.hook_add(UC_HOOK_CODE,hook);u.hook_add(UC_HOOK_MEM_WRITE,write_hook)
  u.emu_start(B+2048,target+4,count=180000)
  self.assertTrue(entered,'AP did not finish within bounded instruction count')
  self.assertEqual(u.reg_read(UC_ARM64_REG_X0),B);self.assertEqual(u.reg_read(UC_ARM64_REG_X1),target);self.assertEqual(u.reg_read(UC_ARM64_REG_X10),cpu<<8)
  self.assertEqual(bytes(u.mem_read(B+16,2048-16)),canary)
  self.assertEqual(bytes(u.mem_read(R,0x40000)),tables,'reader modified page tables/data')
  can_commit=current_el==4 and not sctlr&0x1005
  if can_commit:
   snap=list(struct.unpack('<8Q',u.mem_read(B+0xe00,64)))
   telemetry=list(struct.unpack('<8Q',u.mem_read(B+0xf00,64)))
   journal.commit_reference_slot(zone,cpu,snap,telemetry,[tpidr,LPB,cpu,cpu<<8,snap[0]])
   self.assertEqual(journal_writes[-1],(journal.ZONE_BASE+cpu*journal.SLOT_BYTES,8),'journal commit not final aligned store')
  else:self.assertEqual(journal_writes,[],'journal writer ran with MMU/cache/EL guard failing')
  actual_zone=bytes(u.mem_read(journal.ZONE_BASE,journal.ZONE_BYTES))
  self.assertEqual(actual_zone,bytes(zone),'journal schema/CRC/other-slot preservation differs from reference')
  self.assertEqual(journal.decode(actual_zone,attempt,build)['records'][cpu-1]['valid'],can_commit)
  if cpu!=1:
   self.assertEqual(helper_calls,0);self.assertEqual(bytes(u.mem_read(B+0xe00,64)),bytes(64));return
  status={'invalid_tpidr':(2<<32)|1,'invalid_table':(1<<32)|3,'out_of_ram':(1<<32)|5,'out_of_ram_leaf':(1<<32)|5,'wrong_tcr':(1<<32)|6}.get(case,0)
  if case.startswith('invalid_tpidr_l'):status=(2<<32)|3
  vals=[status,tpidr,0 if status and status>>32==1 else lpb_prcb,0 if status else 1,0 if status else local,0 if status else shared,0xfffff80000a91ba0,ttbr]
  if case=='invalid_table':vals[3:6]=[0,root+((LPB>>39)&255)*8,0]
  if case=='invalid_tpidr_l0':vals[3:6]=[0,root+((tpidr>>39)&255)*8,0]
  if case=='invalid_tpidr_l1':vals[3:6]=[1,R+0x1000+((tpidr>>30)&511)*8,0]
  if case=='invalid_tpidr_l2':vals[3:6]=[2,R+0x6000+((tpidr>>21)&511)*8,0]
  if case=='invalid_tpidr_l3':vals[3:6]=[3,R+0x4000+(((PRCB+0x20)>>12)&511)*8,0xdead0000]
  self.assertEqual(list(struct.unpack('<8Q',u.mem_read(B+0xe00,64))),vals)
  self.assertEqual(helper_calls,2 if case.startswith('invalid_tpidr') else 1 if status else 4)
  expected={}
  for row,value in enumerate(vals):
   for glyph,char in enumerate(f'{value:016x}'):
    for bit,on in enumerate(HEX[int(char,16)]):
     for dy in range(6):
      for dx in range(6):
       pa=FB+(2528+row%4*40+bit//3*6+dy)*4992+((64 if row<4 else 640)+glyph*24+bit%3*6+dx)*4
       expected[pa]=0xffffffff if on=='1' else 0
  for pa,value in expected.items():self.assertEqual(screen.get(pa),value,f'hex pixel mismatch {pa:x}')
  self.assertTrue(all(pa in expected or FB+(512+64*cpu)*4992+64<=pa<FB+(512+64*cpu+16)*4992+128 for pa in screen),'unexpected framebuffer write')
 def test_success_and_bounded_failures(self):
  for case in ['success','zero_queue','null_prcb','invalid_tpidr','lpb_bsp','tpidr_flags','invalid_table','out_of_ram','out_of_ram_leaf','wrong_tcr','invalid_tpidr_l0','invalid_tpidr_l1','invalid_tpidr_l2','invalid_tpidr_l3']:
   with self.subTest(case=case):self.run_case(case)
  self.run_case('success',root_half=True)
 def test_other_aps_do_not_run_reader(self):
  for cpu in range(2,8):
   with self.subTest(cpu=cpu):self.run_case('wrong_tcr',cpu)
 def test_journal_architectural_guard(self):
  for sctlr,el in [(1,4),(4,4),(0x1000,4),(0,8),(0x30d00800,4)]:
   with self.subTest(sctlr=hex(sctlr),el=el):self.run_case('wrong_tcr',2,sctlr=sctlr,current_el=el)
 def test_private_layout(self):
  self.assertLess(len(CODE),0x600)
  self.assertEqual(SYMS['DegasParkingEnd'],len(CODE))
  self.assertLessEqual(0x800+len(CODE),0xe00)
  self.assertLessEqual(0xe00+64,0xf00)
  self.assertLessEqual(0xf38+8,4096)
if __name__=='__main__':unittest.main()
