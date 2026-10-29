"""Publish verified installed Windows EFI files and BCD; keep PE backup."""
from pathlib import Path
import hashlib,json,shlex
from degas_full_windows_stage_source import shell
from stage_degas_ufs_winpe import verify_gpt
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'evidence/raw/2026-10-27-degas-install-success'
N='/data/local/tmp/degas-windows-ntfs'
E='/data/local/tmp/degas-installer-esp'
def sha(p):return shell('sha256sum '+shlex.quote(p)).decode().split()[0]
def main():
    assert shell('getprop ro.product.device').strip()==b'degas'
    verify_gpt()
    shell('mkdir -p '+N+' '+E+' && /data/local/tmp/degas-install/ntfs-3g -o ro,norecover /dev/block/sdc96 '+N)
    try:
        assert sha(N+'/DegasInstall/BCD.full')==hashlib.sha256((OUT/'BCD.full').read_bytes()).hexdigest()
        assert shell('cat '+N+'/DegasInstall/installed.ok')==(OUT/'installed.ok').read_bytes()
        shell('mount -t vfat -o rw /dev/block/sdc94 '+E)
        try:
            old=json.loads((ROOT/'artifacts/degas/full-windows/installer-staged.json').read_text())
            assert sha(E+'/EFI/Microsoft/Boot/BCD')==old['bcd_sha256']
            backup=OUT/'esp-efi-before.tar'
            assert not backup.exists()
            backup.write_bytes(shell('tar -C '+E+' -cf - EFI'))
            target=E+'/EFI/Microsoft/Boot'
            candidate=E+'/EFI/Microsoft/Boot.full-new'
            saved=E+'/EFI/Microsoft/Boot.pre-full'
            shell('test ! -e '+candidate+' && test ! -e '+saved+' && cp -r '+target+' '+candidate)
            for src,dst in [('EFI',''),('Fonts','/Fonts'),('Resources','/Resources')]:
                source=N+'/Windows/Boot/'+src
                shell('mkdir -p '+candidate+dst+' && cp -r '+source+'/. '+candidate+dst+'/')
            shell('cp '+N+'/DegasInstall/BCD.full '+candidate+'/BCD && sync')
            hashes={}
            for name in ['bootmgfw.efi','bootmgr.efi']:
                expected=hashlib.sha256((OUT/name).read_bytes()).hexdigest()
                assert sha(candidate+'/'+name)==expected
                hashes[name]=expected
            assert sha(candidate+'/BCD')==sha(N+'/DegasInstall/BCD.full')
            shell('test ! -e '+E+'/EFI/Boot/bootaa64.efi.pre-full && cp '+E+'/EFI/Boot/bootaa64.efi '+E+'/EFI/Boot/bootaa64.efi.pre-full')
            shell('cp '+candidate+'/bootmgfw.efi '+E+'/EFI/Boot/bootaa64.efi.new && sync')
            assert sha(E+'/EFI/Boot/bootaa64.efi.new')==hashes['bootmgfw.efi']
            shell('mv '+target+' '+saved+' && mv '+candidate+' '+target+' && mv -f '+E+'/EFI/Boot/bootaa64.efi.new '+E+'/EFI/Boot/bootaa64.efi && sync')
            assert sha(target+'/BCD')==sha(N+'/DegasInstall/BCD.full')
            assert sha(E+'/EFI/Boot/bootaa64.efi')==hashes['bootmgfw.efi']
            (OUT/'published.json').write_text(json.dumps(dict(efi_hashes=hashes,bcd_sha256=sha(target+'/BCD'),backup=str(backup),readback='passed'),indent=2))
        finally:shell('umount '+E)
    finally:shell('umount '+N)
    verify_gpt();print('Published full Windows boot; old EFI backed up; GPT unchanged')
if __name__=='__main__':main()
