"""Read installed Windows evidence through Android NTFS, without writing it."""
from pathlib import Path
from degas_full_windows_stage_source import shell
from stage_degas_ufs_winpe import verify_gpt
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'evidence/raw/2026-10-27-degas-install-success'
M='/data/local/tmp/degas-windows-ntfs'
def main():
    assert shell('getprop ro.product.device').strip()==b'degas'
    verify_gpt();OUT.mkdir(exist_ok=True)
    shell('mkdir -p '+M+' && /data/local/tmp/degas-install/ntfs-3g -o ro,norecover /dev/block/sdc96 '+M)
    try:
        rows=[r.split() for r in shell('cat /proc/mounts').decode().splitlines() if r.split()[1]==M]
        assert len(rows)==1 and rows[0][0]=='/dev/block/sdc96' and 'ro' in rows[0][3].split(',')
        for n in ['installed.ok','apply.log','ufs-install.log','touch-install.log','BCD.full','BCD.full.txt','BCD.before','bootstrap.log']:
            (OUT/n).write_bytes(shell('cat '+M+'/DegasInstall/'+n))
        (OUT/'SYSTEM').write_bytes(shell('cat '+M+'/Windows/System32/config/SYSTEM'))
        (OUT/'boot-files.txt').write_bytes(shell('ls -lR '+M+'/Windows/Boot'))
        for n in ['bootmgfw.efi','bootmgr.efi']:
            (OUT/n).write_bytes(shell('cat '+M+'/Windows/Boot/EFI/'+n))
        (OUT/'driver-hashes.txt').write_bytes(shell('find '+M+'/Windows/System32/DriverStore/FileRepository -type f \\( -name degasufs.sys -o -name degastouch.sys \\) -exec sha256sum {} \\;'))
    finally:shell('umount '+M)
    print('Captured installed Windows evidence; NTFS unmounted')
if __name__=='__main__':main()
