"""Stage verified file-only maintenance on exact Windows NTFS and ESP."""
from pathlib import Path
import hashlib,json,shlex,subprocess
from degas_full_windows_stage_source import shell,ADB
from stage_degas_ufs_winpe import verify_gpt
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/degas/setup-maintenance'
N='/data/local/tmp/degas-windows-ntfs';E='/data/local/tmp/degas-installer-esp'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def remote_sha(p):return shell('sha256sum '+shlex.quote(p)).decode().split()[0]
def push(p,target):
    temp='/data/local/tmp/degas-maintenance-file'
    subprocess.run(ADB+['push',str(p),temp],check=True,stdout=subprocess.DEVNULL)
    assert remote_sha(temp)==sha(p)
    shell('cp '+temp+' '+shlex.quote(target)+' && sync')
    assert remote_sha(target)==sha(p)
def main():
    assert shell('getprop ro.product.device').strip()==b'degas'
    verify_gpt()
    pkg=ROOT/'artifacts/degas/ufs-windows/performance-candidate'
    manifest=json.loads((pkg/'package.json').read_text());assert manifest['performance_candidate'] and manifest['maximum_transfer_bytes']==262144
    shell('/data/local/tmp/degas-install/ntfs-3g -o rw,norecover,big_writes /dev/block/sdc96 '+N)
    files={}
    try:
        assert shell('test -f '+N+'/DegasInstall/installed.ok && echo YES').strip()==b'YES'
        shell('mkdir -p '+N+'/DegasInstall/Ufs02')
        for name,digest in manifest['files'].items():
            p=pkg/'test-package'/name;assert sha(p)==digest
            target=N+'/DegasInstall/Ufs02/'+name;push(p,target);files[target]=digest
        for name in ['maintenance.cmd','maintenance.once','degas-maintenance-verify.exe','unattend-retry.xml','BCD.system']:
            p=OUT/name;target=N+'/DegasInstall/'+name;push(p,target);files[target]=sha(p)
    finally:shell('umount '+N)
    pe=json.loads((OUT/'pe-package.json').read_text());wim=Path(pe['wim']);assert sha(wim)==pe['sha256']
    bcd=json.loads((OUT/'bcd-package.json').read_text())
    shell('mount -t vfat -o rw /dev/block/sdc94 '+E)
    try:
        t=E+'/EFI/Microsoft/Boot/BCD';assert remote_sha(t)==bcd['BCD.source']
        expected=json.loads((ROOT/'artifacts/degas/full-windows/installer-staged.json').read_text())['wim_sha256']
        target=E+'/sources/boot.wim';assert remote_sha(target)==expected
        assert shell('test ! -e '+target+'.pre-maintenance && echo YES').strip()==b'YES'
        # Stage and verify the WIM before making PE the default entry.
        new=target+'.maintenance-new';subprocess.run(ADB+['push',str(wim),'/data/local/tmp/degas-maintenance.wim'],check=True,stdout=subprocess.DEVNULL)
        assert remote_sha('/data/local/tmp/degas-maintenance.wim')==pe['sha256']
        shell('cp /data/local/tmp/degas-maintenance.wim '+new+' && sync');assert remote_sha(new)==pe['sha256']
        shell('mv '+target+' '+target+'.pre-maintenance && mv '+new+' '+target+' && sync')
        assert remote_sha(target)==pe['sha256']
        push(OUT/'BCD.maintenance',t+'.maintenance-new')
        shell('mv -f '+t+'.maintenance-new '+t+' && sync');assert remote_sha(t)==bcd['BCD.maintenance']
    finally:shell('umount '+E)
    verify_gpt();(OUT/'staged.json').write_text(json.dumps(dict(files=files,wim_sha256=pe['sha256'],bcd_sha256=bcd['BCD.maintenance'],raw_journal=False,gpt_unchanged=True,readback='passed'),indent=2))
    print('Maintenance staged; next boot is PE maintenance, no image application')
if __name__=='__main__':main()
