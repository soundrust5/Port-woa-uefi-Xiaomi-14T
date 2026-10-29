"""Persist the disk identity previously recorded by WinPE, no partition edits."""
from pathlib import Path
import hashlib,json,struct,uuid,zlib,subprocess
from degas_full_windows_stage_source import shell,ADB
from stage_degas_ufs_winpe import verify_gpt,PLAN
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'evidence/raw/2026-10-27-degas-disk-identity'
GUID=uuid.UUID('38e83f8c-f006-11ee-8c8b-e7cf9b776223')
def read(lba,n=1):return shell(f'dd if=/dev/block/sdc bs=4096 skip={lba} count={n} 2>/dev/null')
def valid(data):
    b=bytearray(data);assert len(b)==4096 and b[:8]==b'EFI PART'
    size=struct.unpack_from('<I',b,12)[0];assert size==92
    crc=struct.unpack_from('<I',b,16)[0];struct.pack_into('<I',b,16,0)
    assert zlib.crc32(b[:size])==crc
def main():
    assert shell('getprop ro.product.device').strip()==b'degas'
    verify_gpt()
    assert int(shell('blockdev --getsize64 /dev/block/sdc'))==264509587456
    first=read(0,16);last=read(64577528,8)
    (OUT/'identity-before-first64k.bin').write_bytes(first)
    (OUT/'identity-before-last32k.bin').write_bytes(last)
    candidates={}
    for name,lba in [('new-primary.bin',1),('new-backup.bin',64577535)]:
        before=read(lba);valid(before);assert before==(PLAN/name).read_bytes()
        assert before[56:72]==bytes(16)
        after=bytearray(before);after[56:72]=GUID.bytes_le
        struct.pack_into('<I',after,16,0);struct.pack_into('<I',after,16,zlib.crc32(after[:92]));valid(after)
        assert all(before[i]==after[i] for i in range(4096) if i not in range(16,20) and i not in range(56,72))
        path=OUT/('identity-'+name);path.write_bytes(after);candidates[name]=(path,lba,bytes(after))
        remote='/data/local/tmp/'+path.name
        subprocess.run(ADB+['push',str(path),remote],check=True)
        assert shell('cat '+remote)==bytes(after)
    for name in ['new-backup.bin','new-primary.bin']:
        path,lba,data=candidates[name]
        shell(f'dd if=/data/local/tmp/{path.name} of=/dev/block/sdc bs=4096 seek={lba} count=1 conv=notrunc && sync')
        assert read(lba)==data
    assert read(0,16)==first[:4096]+candidates['new-primary.bin'][2]+first[8192:]
    assert read(64577528,8)==last[:-4096]+candidates['new-backup.bin'][2]
    manifest={'disk_guid':str(GUID),'partition_entries_unchanged':True,'headers':{n:{'path':str(p),'sha256':hashlib.sha256(d).hexdigest()} for n,(p,l,d) in candidates.items()}}
    (ROOT/'artifacts/degas/full-windows/gpt-identity.json').write_text(json.dumps(manifest,indent=2))
    print('Both disk-GUID headers verified, all other first/last blocks unchanged')
if __name__=='__main__':main()
