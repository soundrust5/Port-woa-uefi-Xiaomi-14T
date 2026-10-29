"""Read-only storage inventory and first 64KiB of each physical LUN."""
import hashlib,json,subprocess,shlex
from pathlib import Path
root=Path('evidence/raw/2026-10-25-degas-ufs-baseline-v2')
root.mkdir(exist_ok=False)
def capture(name,command):
    r=subprocess.run(['C:/adb/adb.exe','-s','XC7TUCXG95U8IVMN','exec-out',
                      'su -c '+shlex.quote(command)],capture_output=True,timeout=30)
    if r.returncode: raise RuntimeError((command,r.stderr))
    (root/name).write_bytes(r.stdout)
    records.append(dict(file=name,command=command,bytes=len(r.stdout),sha256=hashlib.sha256(r.stdout).hexdigest()))
records=[]
capture('partitions.txt','cat /proc/partitions; ls -l /dev/block/by-name')
capture('host.txt','readlink /sys/bus/platform/devices/112b0000.ufshci/driver; cat /sys/bus/platform/devices/112b0000.ufshci/uevent')
capture('live-fdt.dtb','cat /sys/firmware/fdt')
for disk in ['sda','sdb','sdc']:
    capture(disk+'.txt',f'cat /sys/block/{disk}/size /sys/block/{disk}/queue/logical_block_size /sys/block/{disk}/device/vendor /sys/block/{disk}/device/model; readlink -f /sys/block/{disk}/device')
    capture(disk+'-first64k.bin',f'dd if=/dev/block/{disk} bs=4096 count=16 2>/dev/null')
    if (root/(disk+'-first64k.bin')).stat().st_size!=65536:raise RuntimeError('short read')
for item in ['specification_version','manufacturer_id','number_of_luns','boot_enable','queue_depth']:
    capture(item+'.txt','cat /sys/bus/platform/devices/112b0000.ufshci/device_descriptor/'+item)
(root/'manifest.json').write_text(json.dumps(records,indent=2))
print(json.dumps(records,indent=2))
