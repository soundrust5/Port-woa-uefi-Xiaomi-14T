import struct
import test_entry as entry
import test_ramoops_entry as ram
ram.BUILD_NAME='build-display-fullscreen'
a=ram.Artifacts()
seed={0x50746e5c:0xa9bd7bfd,0x50746e60:0xf9000bf5,0x5082e570:1,
0x14402f40:0xfcc2f000,0x14402044:4992,0x14402038:0xa98026d,
0x14602f40:0xfcc2f95c,0x14602044:4992,0x14602038:0xa98026d,
0x1400d100:0x55,0x1400d104:0x1f1f01,0x14001030:0xbc7423ff,0x14401030:0x8007,0x14201030:0x9c6083ff,0x14601030:0x8007}
expected=[(0x1400d190,2),(0x1400d190,6),(0x1400d008,0),(0x1400d014,0),
(0x1400d100,0x55),(0x1400d104,0x1f1f01),(0x14201008,0),(0x14201020,1),(0x14601008,0),(0x14601020,1),(0x14001008,0),(0x14001020,1),
(0x14401008,0),(0x14401020,1),(0x1400dd00,0x2c3909),(0x1400d060,0x8001),
(0x1400d000,0),(0x1400d000,1)]
for base in entry.LOAD_BASES:
 for case in ('ok','guard','busy'):
  regs={entry.DAIF:0x3c0,entry.CURRENT_EL:4,entry.SCTLR_EL1:0x30d00800,entry.TCR_EL1:0,entry.TTBR0_EL1:0,entry.MAIR_EL1:0}
  r=ram.Emulation(a,base,regs)
  for addr,size in [(0x50700000,0x200000),(0x48090000,0xe0000),(0xfcc2f000,0xd00000),(0x1400d000,0x1000),(0x14001000,0x1000),(0x14201000,0x1000),(0x14401000,0x2000),(0x14601000,0x2000)]:r.uc.mem_map(addr,size)
  for addr,v in seed.items():r.uc.mem_write(addr,struct.pack('<I',v))
  if case=='guard':r.uc.mem_write(0x5082e570,struct.pack('<I',2))
  if case=='busy':r.uc.mem_write(0x1400d00c,struct.pack('<I',0x80000000))
  r.uc.emu_start(base,r.image_end,count=100000000)
  assert r.stop_symbol==('diagnostic_ramoops_ok' if case=='ok' else 'diagnostic_ramoops_failed')
  mmio=[];pixels=0
  for addr,size,v in r.writes:
   if 0x14000000<=addr<0x14700000:
    assert size==4 and addr%4==0;mmio.append((addr,v))
   elif 0xfcc2f000<=addr<0xfd92f000:
    off=addr-0xfcc2f000;y,x=divmod(off,4992)
    assert 0<=y<2712 and 0<=x<1220*4 and size==4 and x%4==0;pixels+=1
   else:assert 0x480af000<=addr and addr+size<=0x480ef000 or base+a.symbols['entry_record']<=addr and addr+size<=base+a.symbols['__stack_end']
  assert mmio==(expected if case=='ok' else [])
  assert (pixels>=1220*2712 if case=='ok' else pixels==0)
print('PASS: two load bases; exact MMIO order; marker bounds; wrong-mode and stuck-busy cause no hardware writes')
