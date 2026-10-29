"""Read primary/backup GPT metadata from the observed degas UFS user LUN.

Reads metadata blocks only. Validates CRCs, geometry and partition overlaps.
Never writes the block device. Not a full storage or personal-data backup.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import zlib


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args()
    if args.output.exists():
        raise SystemExit('Use a new output directory')
    devices=[l.split()[0] for l in subprocess.check_output(['adb','devices'],text=True).splitlines() if '\tdevice' in l]
    if len(devices)!=1:
        raise SystemExit('Exactly one ADB device required')
    prefix=['adb','-s',devices[0],'exec-out','su','-c']
    def command(cmd):
        p=subprocess.run(prefix+[cmd],capture_output=True,timeout=30)
        if p.returncode:
            raise RuntimeError(p.stderr.decode(errors='replace'))
        return p.stdout
    identity=command('id; getprop ro.product.device; readlink -f /dev/block/by-name/userdata')
    if b'uid=0' not in identity or b'degas' not in identity or b'/dev/block/sdc92' not in identity:
        raise SystemExit('Observed device/layout changed; inspect before collecting')
    sector=int(command('blockdev --getss /dev/block/sdc'))
    size=int(command('blockdev --getsize64 /dev/block/sdc'))
    if sector not in (512,4096) or size%sector:
        raise ValueError('Invalid device geometry')
    total_lbas=size//sector
    args.output.mkdir(parents=True)
    reads=[]
    def read(lba,count,name):
        if lba<0 or count<=0 or lba+count>total_lbas or count*sector>4*1024*1024:
            raise ValueError('GPT read out of bounds')
        cmd=f'dd if=/dev/block/sdc bs={sector} skip={lba} count={count} 2>/dev/null'
        data=command(cmd)
        if len(data)!=count*sector:
            raise ValueError('Short metadata read')
        (args.output/name).write_bytes(data)
        reads.append({'file':name,'command':cmd,'lba':lba,'count':count,'sha256':hashlib.sha256(data).hexdigest()})
        return data
    read(0,1,'protective-mbr.bin')
    def header(lba,name):
        raw=read(lba,1,name+'.bin')
        fields=struct.unpack_from('<8sIIIIQQQQ16sQIII',raw)
        sig,rev,hs,crc,res,current,backup,first,last,guid,entry_lba,count,es,entry_crc=fields
        if sig!=b'EFI PART' or not 92<=hs<=sector or current!=lba or backup>=total_lbas or not 2<=first<=last<total_lbas or not 0<count<=16384 or not 128<=es<=4096:
            raise ValueError('Invalid GPT header')
        check=bytearray(raw[:hs]); check[16:20]=b'\0'*4
        if zlib.crc32(check)!=crc:
            raise ValueError('GPT header CRC mismatch')
        data=read(entry_lba,(count*es+sector-1)//sector,name+'-entries.bin')[:count*es]
        if zlib.crc32(data)!=entry_crc:
            raise ValueError('GPT entry array CRC mismatch')
        return {'current_lba':current,'backup_lba':backup,'first_usable_lba':first,'last_usable_lba':last,'entry_lba':entry_lba,'entry_count':count,'entry_size':es,'disk_guid_hex':guid.hex(),'header_crc_valid':True,'entries_crc_valid':True},data
    primary,entries=header(1,'primary-gpt')
    backup,backup_entries=header(total_lbas-1,'backup-gpt')
    if primary['backup_lba']!=backup['current_lba'] or backup['backup_lba']!=1 or entries!=backup_entries or any(primary[k]!=backup[k] for k in ('disk_guid_hex','first_usable_lba','last_usable_lba','entry_count','entry_size')):
        raise ValueError('Primary/backup GPT disagreement')
    partitions=[]
    for i in range(primary['entry_count']):
        record=entries[i*primary['entry_size']:(i+1)*primary['entry_size']]
        if record[:16]==b'\0'*16:
            continue
        first,last,attrs=struct.unpack_from('<QQQ',record,32)
        if not primary['first_usable_lba']<=first<=last<=primary['last_usable_lba']:
            raise ValueError('Partition outside usable region')
        partitions.append({'index':i+1,'name':record[56:128].decode('utf-16-le').rstrip('\0'),'first_lba':first,'last_lba':last,'start_bytes':first*sector,'size_bytes':(last-first+1)*sector,'attributes':hex(attrs)})
    ordered=sorted(partitions,key=lambda p:p['first_lba'])
    for a,b in zip(ordered,ordered[1:]):
        if a['last_lba']>=b['first_lba']:
            raise ValueError('Overlapping GPT partitions')
    result={'device':'/dev/block/sdc','logical_sector_bytes':sector,'device_bytes':size,'primary':primary,'backup':backup,'partitions':partitions,'reads':reads,'checks':'Both CRCs, mirrored entries, bounds, no overlaps','caveat':'Metadata backup only; filesystem resize correctness is not established by GPT checks.'}
    (args.output/'gpt.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(f'GPT verified: {len(partitions)} partitions; sector={sector}; bytes={size}')
    for p in partitions:
        if p['name'] in ('userdata','linux_root','tee_a','tee_b'):
            print(p)


if __name__=='__main__':
    main()
