"""Read back maintenance result, exact BCD, boot hive and driver bytes."""
from pathlib import Path
import hashlib,json
from degas_full_windows_stage_source import shell
from stage_degas_ufs_winpe import verify_gpt
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'evidence/raw/2026-10-27-degas-maintenance-success'
N='/data/local/tmp/degas-windows-ntfs';E='/data/local/tmp/degas-installer-esp'
def main():
    verify_gpt();OUT.mkdir(exist_ok=True);assert not any(OUT.iterdir())
    shell('/data/local/tmp/degas-install/ntfs-3g -o ro,norecover /dev/block/sdc96 '+N)
    try:
        for name in ['maintenance-complete.ok','ufs02-install.log','bootstrap.log']:
            (OUT/name).write_bytes(shell('cat '+N+'/DegasInstall/'+name))
        (OUT/'SYSTEM').write_bytes(shell('cat '+N+'/Windows/System32/config/SYSTEM'))
        (OUT/'driver-hashes.txt').write_bytes(shell('find '+N+'/Windows/System32/DriverStore/FileRepository -type f -name degasufs.sys -exec sha256sum {} \\;'))
        (OUT/'unattend.xml').write_bytes(shell('cat '+N+'/Windows/Panther/unattend.xml'))
    finally:shell('umount '+N)
    shell('mount -t vfat -o ro /dev/block/sdc94 '+E)
    try:(OUT/'BCD').write_bytes(shell('cat '+E+'/EFI/Microsoft/Boot/BCD'))
    finally:shell('umount '+E)
    expected=ROOT/'artifacts/degas/setup-maintenance/BCD.system'
    assert (OUT/'BCD').read_bytes()==expected.read_bytes()
    assert b'd1d41db322bbc47bb7554f5d8a589d4708e21f5082c697e0d89f18d73d9b2d88' in (OUT/'driver-hashes.txt').read_bytes()
    (OUT/'capture.json').write_text(json.dumps({p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in OUT.iterdir() if p.is_file()},indent=2))
    print('Result, UFS0.2 driver bytes and fullWindows system BCD verified')
if __name__=='__main__':main()
