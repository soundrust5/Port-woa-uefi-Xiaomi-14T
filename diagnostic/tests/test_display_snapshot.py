"""Execute survey ELF, enforce alignment and absence of MMIO writes."""
import struct,sys
import test_entry as entry
import test_ramoops_entry as ram
from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
ram.BUILD_NAME=sys.argv[1] if len(sys.argv)>1 else 'build-display-snapshot'
a=ram.Artifacts()
for base in entry.LOAD_BASES:
    regs={entry.DAIF:0x3c0,entry.CURRENT_EL:4,entry.SCTLR_EL1:0x30d00800,
          entry.TCR_EL1:0,entry.TTBR0_EL1:0,entry.MAIR_EL1:0}
    r=ram.Emulation(a,base,regs)
    for address,size in [(0x50700000,0x200000),(0x48090000,0xe0000),(0xfcc2f000,0x100000),
                         (0x1400d000,0x1000),(0x14001000,0x1000),(0x14401000,0x2000),
                         (0x1420d000,0x1000),(0x14201000,0x1000),(0x14601000,0x2000)]:
        r.uc.mem_map(address,size)
    faults=[]
    def access(uc,kind,address,size,value,data):
        if size>1 and address%size:
            faults.append(address);uc.emu_stop()
    r.uc.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,access)
    r.uc.emu_start(base,r.image_end,count=4000000)
    assert not faults and r.stop_symbol=='diagnostic_ramoops_ok'
    for address,size,value in r.writes:
        assert (0x480af000<=address and address+size<=0x480ef000) or (
            base+a.symbols['entry_record']<=address and address+size<=base+a.symbols['__stack_end'])
    raw=bytes(r.uc.mem_read(0x480af000,0x40000))
    sig,start,size=struct.unpack_from('<III',raw)
    text=raw[12+start:12+size]+raw[12:12+start]
    assert b'DISPLAY_SNAPSHOT_DONE' in text
    assert text.count(b'READ_VALUE')==({'build-display-snapshot-v2':23,'build-display-snapshot-v3':40,'build-display-snapshot-v4':47}.get(ram.BUILD_NAME,12))
print('PASS: two load bases, aligned accesses, no framebuffer/MMIO writes, completed snapshot')
