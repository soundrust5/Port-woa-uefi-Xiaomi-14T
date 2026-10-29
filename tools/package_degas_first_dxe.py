"""Compose loader-owned low RAM arena and actual degas FD; no device writes."""
from pathlib import Path
import struct,gzip,hashlib,json,sys
root=Path(__file__).resolve().parents[1]
build_log=(root/'evidence/build/degas-first-dxe.log').read_text(errors='replace')
if '\n- Done -\n' not in build_log or '\n- Failed -\n' in build_log:
 raise SystemExit('Refusing stale FD: the latest degas firmware build did not complete successfully')
out=root/'artifacts'/(sys.argv[1] if len(sys.argv)>1 else 'degas-first-dxe');out.mkdir(exist_ok=True)
fd=(root/'mu/Build/degasPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd').read_bytes()
assert len(fd)==0x200000 and fd[40:44]==b'_FVH'
hlen=struct.unpack_from('<H',fd,48)[0]
assert sum(struct.unpack('<'+'H'*(hlen//2),fd[:hlen]))&0xffff==0
word=struct.unpack_from('<I',fd)[0];assert word>>26==5
imm=word&0x3ffffff
if imm&(1<<25):imm-=1<<26
entry=imm*4;assert 0<entry<len(fd)
wrapper=(root/'diagnostic/build-mu-handoff/entry.bin').read_bytes()
assert len(wrapper)<0x200000 and wrapper[56:60]==b'ARM\x64'
image=bytearray(0x4480000)
image[:len(wrapper)]=wrapper;image[0x200000:0x400000]=fd
struct.pack_into('<Q',image,16,len(image))
# Excludes observed DTB47c80000, all static low carveouts >=48080000,
# retained LK50700000, initrd above low arena, framebuffer and ramoops.
assert 0x40000000+len(image)==0x44480000<0x47c80000<0x48080000
assert image[0x400000:]==bytes(len(image)-0x400000)
packed=gzip.compress(image,compresslevel=9,mtime=0)
assert gzip.decompress(packed)==image
(out/'kernel.bin.gz').write_bytes(packed);(out/'SILICIUM_UEFI.fd').write_bytes(fd)
(out/'wrapper-manifest.json').write_bytes((root/'diagnostic/build-mu-handoff/manifest.json').read_bytes())
report={'purpose':'experimental degas SEC/DXE/BDS diagnostics; not a Windows-ready firmware','fd_base':'0x40200000','sec_entry':hex(0x40200000+entry),'fd_sha256':hashlib.sha256(fd).hexdigest(),'image_bytes':len(image),'image_sha256':hashlib.sha256(image).hexdigest(),'gzip_sha256':hashlib.sha256(packed).hexdigest(),'gzip_bytes':len(packed),'heap':['0x40480000','0x44440000'],'reserved_gce_dma':['0x44440000','0x44470000'],'reserved_ufs_dma':['0x44470000','0x44480000'],'stack':['0x40400000','0x40440000'],'watchdog':'left enabled','input':'three-button minimal BDS menu','storage':'read-only UFS BlockIO/FAT EFI application test; no Windows storage driver'}
(out/'layout.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
