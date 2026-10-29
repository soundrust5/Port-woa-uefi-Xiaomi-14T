"""Read-only Android UFS/GPT reference for the Windows bring-up. No MMIO."""
import argparse, hashlib, json, shlex, struct, subprocess, uuid, zlib
from pathlib import Path
from collect_probe_ramoops import unpack_capture

ADB = ['C:/adb/adb.exe', '-s', 'XC7TUCXG95U8IVMN']

def read_shell(command):
    token = 'DEGAS_UFS_' + uuid.uuid4().hex
    wrapped = '(' + command + '); task_rc=$?; printf "\\n' + token + ':%s\\n" "$task_rc"'
    r = subprocess.run(ADB + ['exec-out', 'su -c ' + shlex.quote(wrapped)], capture_output=True, timeout=60)
    rc, data = unpack_capture(r.stdout, token)
    if r.returncode or rc != 0:
        raise RuntimeError((command, r.returncode, rc, r.stderr))
    return data

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, required=True)
    out = ap.parse_args().output
    assert read_shell('getprop ro.product.device').strip() == b'degas'
    assert read_shell('getprop sys.boot_completed').strip() == b'1'
    assert read_shell('id -u').strip() == b'0'
    out.mkdir(parents=True, exist_ok=False)
    records = []
    def capture(name, command):
        data = read_shell(command); (out / name).write_bytes(data)
        records.append(dict(file=name, command=command, bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
        return data
    capture('fdt.dtb', 'cat /sys/firmware/fdt')
    capture('partitions.txt', 'cat /proc/partitions; ls -l /dev/block/by-name')
    capture('host-uevent.txt', 'cat /sys/bus/platform/devices/112b0000.ufshci/uevent')
    capture('host-driver.txt', 'readlink -f /sys/bus/platform/devices/112b0000.ufshci/driver')
    disks=[]
    for disk in ('sda','sdb','sdc'):
        size=int(capture(disk+'-bytes.txt',f'blockdev --getsize64 /dev/block/{disk}'))
        block=int(capture(disk+'-block.txt',f'blockdev --getss /dev/block/{disk}'))
        path=capture(disk+'-path.txt',f'readlink -f /sys/block/{disk}/device').decode().strip()
        assert block==4096 and size%block==0 and size>=65536
        first=capture(disk+'-first64k.bin',f'dd if=/dev/block/{disk} bs=4096 count=16 2>/dev/null')
        last=capture(disk+'-last32k.bin',f'dd if=/dev/block/{disk} bs=4096 skip={size//block-8} count=8 2>/dev/null')
        assert len(first)==65536 and len(last)==32768
        entry=dict(android_node=disk, bytes=size, block_size=block, scsi_path=path,
                   lun=int(path.rsplit('/',1)[-1].split(':')[-1]),
                   first64k_sha256=hashlib.sha256(first).hexdigest(),last32k_sha256=hashlib.sha256(last).hexdigest())
        if first[block:block+8]==b'EFI PART':
            h=first[block:2*block]; hs=struct.unpack_from('<I',h,12)[0]
            assert hs==92
            def header_ok(buf):
                b=bytearray(buf[:hs]); crc=struct.unpack_from('<I',b,16)[0];struct.pack_into('<I',b,16,0)
                assert b[:8]==b'EFI PART' and zlib.crc32(b)==crc
            header_ok(h);header_ok(last[-block:])
            lba,n,es=struct.unpack_from('<QII',h,72)
            assert es==128 and n==128 and lba==2
            entries=first[lba*block:lba*block+n*es]
            assert len(entries)==n*es and zlib.crc32(entries)==struct.unpack_from('<I',h,88)[0]
            back_lba=struct.unpack_from('<Q',last[-block:],72)[0]
            pos=(back_lba-(size//block-8))*block
            assert pos>=0 and last[pos:pos+n*es]==entries
            entry['gpt']=dict(disk_guid=str(uuid.UUID(bytes_le=h[56:72])),first_usable=struct.unpack_from('<Q',h,40)[0],last_usable=struct.unpack_from('<Q',h,48)[0],partitions=[])
            for i in range(n):
                e=entries[i*es:(i+1)*es]
                if not any(e[:16]):continue
                start,end=struct.unpack_from('<QQ',e,32)
                assert entry['gpt']['first_usable']<=start<=end<=entry['gpt']['last_usable']
                entry['gpt']['partitions'].append(dict(index=i+1,name=e[56:].decode('utf-16le').rstrip('\0'),guid=str(uuid.UUID(bytes_le=e[16:32])),type=str(uuid.UUID(bytes_le=e[:16])),start=start,end=end))
        disks.append(entry)
    assert sorted(d['lun'] for d in disks)==[0,1,2]
    (out/'reference.json').write_text(json.dumps(dict(device='degas',read_only=True,disks=disks,records=records),indent=2))
    print(json.dumps([dict(lun=d['lun'],bytes=d['bytes'],gpt='gpt' in d) for d in disks],indent=2))

if __name__=='__main__':main()
