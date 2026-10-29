"""Format only the authorized, GUID/geometry-checked ESP and stage pristine PE."""
from pathlib import Path
import hashlib,json,subprocess,struct
from prepare_degas_windows_partitions import shell,read,OUT,ROOT,ADB

def main():
    assert shell('getprop ro.product.device').strip()==b'degas'
    assert read(1,1)==(OUT/'new-primary.bin').read_bytes()
    assert read(2,4)==(OUT/'new-entries.bin').read_bytes()
    assert read(64577535,1)==(OUT/'new-backup.bin').read_bytes()
    plan=json.loads((OUT/'plan.json').read_text())
    for p in plan['partitions']:
        node=f'/dev/block/sdc{p["index"]}'
        assert shell(f'readlink -f /dev/block/by-name/{p["name"]}').strip()==node.encode()
        assert int(shell(f'blockdev --getsize64 {node}'))==p['bytes']
        assert int(shell(f'cat /sys/class/block/sdc{p["index"]}/start'))==p['start']*8
    src=ROOT/'artifacts/oneplus12/windows-boot-inputs'
    manifest=json.loads((src/'manifest.json').read_text())
    files=[(src/'esp-staging'/f['path'],f['path'],f['sha256']) for f in manifest['small_esp']['files']]
    files.append((src/'wim/boot.wim','sources/boot.wim','2490c9c9afdb6e7c3e54ec5653f69b8be3465a2f0e178bf475daf3c005c5a5fc'))
    for local,remote,digest in files:assert hashlib.sha256(local.read_bytes()).hexdigest()==digest,remote
    assert b'/dev/block/sdc94 ' not in shell('cat /proc/mounts')
    dest='/data/local/tmp/degas-pe-esp'
    assert not (OUT/'esp-prepared.json').exists(),'Already prepared; do not format again'
    print(shell('newfs_msdos -F 32 -S 4096 -c 1 -L DEGAS_PE /dev/block/sdc94').decode(),flush=True)
    b=shell('dd if=/dev/block/sdc94 bs=4096 count=1 2>/dev/null')
    assert len(b)==4096 and struct.unpack_from('<H',b,11)[0]==4096 and b[13]==1
    assert struct.unpack_from('<I',b,32)[0]==524288 and b[82:90]==b'FAT32   '
    print(shell(f'mkdir -p {dest} && mount -t vfat -o rw,uid=2000,gid=2000,umask=0077 /dev/block/sdc94 {dest}').decode(),flush=True)
    try:
        for directory in sorted({str(Path(remote).parent).replace('\\','/') for _,remote,_ in files}):
            shell(f'mkdir -p {dest}/{directory}')
        for local,remote,digest in files:
            subprocess.run(ADB+['push',str(local),dest+'/'+remote],check=True,timeout=180)
        shell('sync')
        for _,remote,digest in files:
            actual=shell(f'sha256sum {dest}/{remote}').decode().split()[0]
            assert actual==digest,remote
        (OUT/'esp-prepared.json').write_text(json.dumps(dict(files=len(files),verified=True,wim_sha256=files[-1][2]),indent=2))
        print('All ESP files SHA256 verified',flush=True)
    finally:
        print(shell(f'sync; umount {dest}').decode(),flush=True)
    assert read(2,4)==(OUT/'new-entries.bin').read_bytes()

if __name__=='__main__':main()
