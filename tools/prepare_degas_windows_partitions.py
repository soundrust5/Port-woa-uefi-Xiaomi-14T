"""Split ONLY the user-authorized empty linux_root into ESP/MSR/Windows.
Default prepares files. --apply verifies both old GPT copies again before writes.
No formatting here; reboot Android before using the new partition nodes.
"""
from pathlib import Path
import hashlib,json,shlex,struct,subprocess,sys,uuid,zlib
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'evidence/raw/2026-10-25-degas-windows-partition-plan'
ADB=['C:/adb/adb.exe','-s','XC7TUCXG95U8IVMN']
BLOCK=4096;LAST=64577535;FIRST=29267968;END=64573431
def shell(cmd):
    return subprocess.check_output(ADB+['exec-out','su -c '+shlex.quote(cmd)],timeout=60)
def read(lba,count):return shell(f'dd if=/dev/block/sdc bs=4096 skip={lba} count={count} 2>/dev/null')
def valid(h,e):
    h=bytearray(h);crc=struct.unpack_from('<I',h,16)[0];struct.pack_into('<I',h,16,0)
    assert h[:8]==b'EFI PART' and struct.unpack_from('<I',h,12)[0]==92
    assert zlib.crc32(h[:92])==crc and struct.unpack_from('<II',h,80)==(128,128)
    assert zlib.crc32(e)==struct.unpack_from('<I',h,88)[0]
def name(e):return e[56:].decode('utf-16le').rstrip('\0')
def main():
    assert shell('getprop ro.product.device').strip()==b'degas'
    assert shell('readlink -f /dev/block/by-name/linux_root').strip()==b'/dev/block/sdc94'
    assert int(shell('blockdev --getss /dev/block/sdc'))==BLOCK
    assert int(shell('blockdev --getsize64 /dev/block/sdc'))==(LAST+1)*BLOCK
    assert b'/dev/block/sdc94 ' not in shell('cat /proc/mounts')
    if not OUT.exists():
        OUT.mkdir(parents=True)
        (OUT/'original-first64k.bin').write_bytes(read(0,16))
        (OUT/'original-last32k.bin').write_bytes(read(LAST-7,8))
    first=(OUT/'original-first64k.bin').read_bytes();last=(OUT/'original-last32k.bin').read_bytes()
    assert len(first)==65536 and len(last)==32768
    entries=bytearray(first[8192:24576]);valid(first[4096:8192],entries);valid(last[-4096:],entries)
    assert last[:16384]==entries
    assert struct.unpack_from('<Q',last[-4096:],72)[0]==LAST-7
    assert struct.unpack_from('<QQ',entries,93*128+32)==(FIRST,END)
    assert name(entries[93*128:94*128])=='linux_root'
    assert entries[94*128:96*128]==bytes(256)
    sizes=[524288,4096,END-FIRST+1-524288-4096]
    names=['esp','msr','Windows'];types=['c12a7328-f81f-11d2-ba4b-00a0c93ec93b','e3c9e316-0b5c-4db8-817d-f92df00215ae','ebd0a0a2-b9e5-4433-87c0-68b6b72699c7']
    if (OUT/'new-entries.bin').exists():entries=bytearray((OUT/'new-entries.bin').read_bytes())
    else:
        start=FIRST
        for index,(size,label,kind) in enumerate(zip(sizes,names,types),93):
            e=bytearray(128);e[:16]=uuid.UUID(kind).bytes_le;e[16:32]=uuid.uuid4().bytes_le
            struct.pack_into('<QQQ',e,32,start,start+size-1,0);e[56:56+len(label)*2]=label.encode('utf-16le')
            entries[index*128:(index+1)*128]=e;start+=size
        (OUT/'new-entries.bin').write_bytes(entries)
    # All 93 original non-lab entries, including userdata and flashinfo, identical.
    assert entries[:93*128]==first[8192:8192+93*128]
    assert entries[96*128:]==first[8192+96*128:24576]
    start=FIRST;plan=[]
    for index,(size,label,kind) in enumerate(zip(sizes,names,types),93):
        e=entries[index*128:(index+1)*128]
        assert name(e)==label and e[:16]==uuid.UUID(kind).bytes_le
        assert struct.unpack_from('<QQ',e,32)==(start,start+size-1)
        plan.append(dict(index=index+1,name=label,start=start,end=start+size-1,bytes=size*BLOCK,guid=str(uuid.UUID(bytes_le=bytes(e[16:32])))));start+=size
    occupied=sorted(struct.unpack_from('<QQ',entries,i*128+32) for i in range(128) if entries[i*128:i*128+16]!=bytes(16))
    assert all(a<=b and a>=8 and b<=LAST-8 for a,b in occupied)
    assert all(occupied[i][1]<occupied[i+1][0] for i in range(len(occupied)-1))
    for filename,old in [('new-primary.bin',first[4096:8192]),('new-backup.bin',last[-4096:])]:
        h=bytearray(old);struct.pack_into('<I',h,88,zlib.crc32(entries));struct.pack_into('<I',h,16,0)
        struct.pack_into('<I',h,16,zlib.crc32(h[:92]));valid(h,entries);(OUT/filename).write_bytes(h)
    (OUT/'plan.json').write_text(json.dumps(dict(partitions=plan,unchanged_original_entries=93,userdata_unchanged=True,sector=BLOCK),indent=2))
    print(json.dumps(plan,indent=2),flush=True)
    if '--apply' not in sys.argv:return
    assert read(0,16)==first and read(LAST-7,8)==last, 'GPT changed after planning'
    remote='/data/local/tmp/degas-gpt-plan';shell('mkdir -p '+remote+' && chown 2000:2000 '+remote+' && chmod 700 '+remote)
    for filename in ['new-entries.bin','new-primary.bin','new-backup.bin']:
        subprocess.run(ADB+['push',str(OUT/filename),remote+'/'+filename],check=True)
        assert shell('cat '+remote+'/'+filename)==(OUT/filename).read_bytes()
    for filename,lba,count in [('new-entries.bin',LAST-7,4),('new-backup.bin',LAST,1),('new-entries.bin',2,4),('new-primary.bin',1,1)]:
        shell(f'dd if={remote}/{filename} of=/dev/block/sdc bs=4096 seek={lba} count={count} conv=notrunc && sync')
    newfirst=read(0,16);newlast=read(LAST-7,8)
    assert newfirst==first[:4096]+(OUT/'new-primary.bin').read_bytes()+entries+first[24576:]
    assert newlast==entries+last[16384:-4096]+(OUT/'new-backup.bin').read_bytes()
    (OUT/'applied-first64k.bin').write_bytes(newfirst);(OUT/'applied-last32k.bin').write_bytes(newlast)
    (OUT/'applied.txt').write_text('Both GPT copies verified byte-for-byte. Reboot before formatting.\n')
    print('APPLIED AND VERIFIED. Filesystems untouched. Reboot Android before formatting.')
if __name__=='__main__':main()

