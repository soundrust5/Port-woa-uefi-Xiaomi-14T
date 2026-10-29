"""Historical 620-byte pre-PRCB parking fixture; not a test of current Parking.S.

Current source/reader integration is covered by test_degas_prcb_parking.py.
This fixture keeps the recorded source hash as provenance and pins the tested
linked bytes explicitly, rather than comparing a historical source hash with
the evolved implementation. Does not emulate PSCI or Windows.
"""
from pathlib import Path
import struct,sys,unittest,hashlib,json
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'artifacts/degas/cpu-audit/test-deps'))
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE,UC_HOOK_MEM_WRITE
from unicorn.arm64_const import UC_ARM64_REG_X0,UC_ARM64_REG_X1,UC_ARM64_REG_X2,UC_ARM64_REG_X3,UC_ARM64_REG_X10,UC_ARM64_REG_PC
CODE=(ROOT/'artifacts/degas/cpu-audit/parking-v2-code.bin').read_bytes()
PROVENANCE=json.loads((ROOT/'artifacts/degas/cpu-audit/parking-v2-code.json').read_text())
assert PROVENANCE['source_sha256']=='b6924814afa64bf37cbf5dbf16ccd961edc70bfbf261cea771bd94ffe417611f'
assert PROVENANCE['code_sha256']==hashlib.sha256(CODE).hexdigest()=='304710834ab1d0862725cdef5b614ffc7e41c223b5fad82967fb1ed92801bb76'
assert PROVENANCE['linked_at_zero'] and len(CODE)==620
FB=0xfcc2f000
HEX=['111101101101111','010110010010111','111001111100111','111001111001111',
 '101101111001001','111100111001111','111100111101111','111001001001001',
 '111101111101111','111101111001111','111101111101101','110101110101110',
 '111100100100111','110101101101110','111100111100111','111100111100100']

class ParkingCodeTest(unittest.TestCase):
 def run_case(self,base,cpu,mode,sctlr=0):
  u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);target=base+0x10000
  u.mem_map(base,4096);u.mem_map(target,4096);u.mem_map(FB,0xcf0000)
  u.mem_write(base,b'\0'*4096);u.mem_write(base+16,b'\xa5'*(2048-16));u.mem_write(base+2048,CODE)
  observed=[0x0123456789abcdef,0xfedcba9876543210,0x8000000000000001,0x1111222233334444,0x5555666677778888,0x9999aaaabbbbcccc,0xddddeeeeffff0000]
  for offset,value in zip([0x10,0x40,0x3f8,0x368,0x460,0x468,0xe0],observed):u.mem_write(base+offset,struct.pack('<Q',value))
  os_canary=bytes(u.mem_read(base+16,2048-16))
  u.reg_write(UC_ARM64_REG_X0,0) # Reproduce lost incoming PSCI context.
  tick=[0];entered=[False];fbwrites=[]
  expected_pixels={FB+(512+64*cpu+y)*4992+(16+x)*4 for y in range(16) for x in range(16)}
  text_pixels={}
  if cpu==1:
   for row,value in enumerate([target]+observed):
    for glyph,char in enumerate(f'{value:016x}'):
     for bit,on in enumerate(HEX[int(char,16)]):
      for dy in range(6):
       for dx in range(6):
        address=FB+(2528+(row%4)*40+(bit//3)*6+dy)*4992+((64 if row<4 else 640)+glyph*24+(bit%3)*6+dx)*4
        text_pixels[address]=0xffffffff if on=='1' else 0
  text_writes={}
  def write_hook(uc,access,address,size,value,data):
   if FB<=address<FB+0xcf0000:
    if address in expected_pixels:
     self.assertEqual(size,4);fbwrites.append(value)
    else:
     self.assertIn(size,[4,8]) # Unicorn may split STP into two writes.
     for part in range(size//4):
      self.assertTrue(address+part*4 in text_pixels,f'screen write escaped assigned diagnostics: {address+part*4:x}')
      text_writes[address+part*4]=(value>>(part*32))&0xffffffff
  def hook(uc,pc,size,data):
   instruction=bytes(uc.mem_read(pc,4)) if pc!=target else b''
   if instruction==bytes.fromhex('a10038d5'): # MPIDR_EL1
    uc.reg_write(UC_ARM64_REG_X1,0x81000000|(cpu<<8));uc.reg_write(UC_ARM64_REG_PC,pc+4);return
   if pc==target:
    entered[0]=True;uc.emu_stop();return
   if instruction==bytes.fromhex('021038d5'): # SCTLR_EL1
    uc.reg_write(UC_ARM64_REG_X2,sctlr);uc.reg_write(UC_ARM64_REG_PC,pc+4);return
   if instruction==bytes.fromhex('434238d5'): # CurrentEL
    uc.reg_write(UC_ARM64_REG_X3,4);uc.reg_write(UC_ARM64_REG_PC,pc+4);return
   # Wait until READY has been committed after the white square completes.
   if struct.unpack('<Q',uc.mem_read(base+0xc00,8))[0]!=1:return
   tick[0]+=1
   if tick[0]==40 and mode!='id_only':uc.mem_write(base+8,struct.pack('<Q',target))
   if tick[0]==80 and mode!='entry_only':uc.mem_write(base,struct.pack('<I',17 if mode=='wrong_id' else 0 if mode=='wrong_zero' else cpu<<8))
  u.hook_add(UC_HOOK_CODE,hook)
  u.hook_add(UC_HOOK_MEM_WRITE,write_hook)
  u.emu_start(base+2048,target+4,count=150000 if cpu==1 and mode=='both' else 5000)
  self.assertEqual(bytes(u.mem_read(base+16,2048-16)),os_canary,'OS-owned data overwritten')
  self.assertEqual(struct.unpack('<QQ',u.mem_read(base+0xc00,16)),(1,0x81000000|(cpu<<8)))
  self.assertEqual(entered[0],mode=='both')
  self.assertEqual(fbwrites[:256],[0xffffffff]*256)
  if entered[0]:
   self.assertEqual(u.reg_read(UC_ARM64_REG_X0),base)
   self.assertEqual(u.reg_read(UC_ARM64_REG_X1),target)
   self.assertEqual(u.reg_read(UC_ARM64_REG_X10),cpu<<8)
   self.assertEqual(struct.unpack('<Q',u.mem_read(base+8,8))[0],0)
   self.assertEqual(struct.unpack('<QQQQQ',u.mem_read(base+0xc10,40)),(sctlr,4,cpu<<8,target,1))
   expected_color=0xffffbf00 if sctlr&0x1005 else 0xff00ff00
   self.assertEqual(fbwrites[256:],[expected_color]*256)
   if cpu==1:
    self.assertEqual(struct.unpack('<8Q',u.mem_read(base+0xe00,64)),tuple([target]+observed))
    self.assertEqual(text_writes,text_pixels,'displayed hex digits differ from captured startup words')
   else:
    self.assertEqual(text_writes,{})
    self.assertEqual(bytes(u.mem_read(base+0xe00,64)),bytes(64))
  else:
   self.assertEqual(len(fbwrites),512 if mode.startswith('wrong_') else 256)
   self.assertEqual(struct.unpack('<Q',u.mem_read(base+0xc30,8))[0],0)
   if mode.startswith('wrong_'):
    self.assertEqual(fbwrites[256:],[0xffff0000]*256)
    self.assertEqual(struct.unpack('<Q',u.mem_read(base+0xc38,8))[0],17 if mode=='wrong_id' else 0)
 def test_release_and_partial_handshakes(self):
  for base in [0x43000000,0x340000000]:
   for cpu in range(1,8):
    for mode in ['both','entry_only','id_only','wrong_id','wrong_zero']:
     with self.subTest(base=hex(base),cpu=cpu,mode=mode):self.run_case(base,cpu,mode)
 def test_architectural_state_marker_and_capture(self):
  for cpu in range(1,8):
   for sctlr in [1,4,0x1000,0x1005,0x30d00800]:
    with self.subTest(cpu=cpu,sctlr=hex(sctlr)):self.run_case(0x43000000,cpu,'both',sctlr)
 def test_code_fits_firmware_reserved_budget(self):
  self.assertLess(len(CODE),0x400)

if __name__=='__main__':unittest.main()
