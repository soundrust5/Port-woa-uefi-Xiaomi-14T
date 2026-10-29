"""Stage user-authorized one-shot fullOS setup completion, with hive backups."""
from pathlib import Path
import hashlib,json,subprocess
from degas_full_windows_stage_source import shell,ADB
from stage_degas_ufs_winpe import verify_gpt
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/degas/setup-finish'
M='/data/local/tmp/degas-windows-ntfs'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def remote_sha(p):return shell('sha256sum '+p).decode().split()[0]
def push(p,target):
    temp='/data/local/tmp/degas-finish-file'
    subprocess.run(ADB+['push',str(p),temp],check=True,stdout=subprocess.DEVNULL)
    assert remote_sha(temp)==sha(p)
    shell('cp '+temp+' '+target+' && sync');assert remote_sha(target)==sha(p)
def main():
    assert shell('getprop ro.product.device').strip()==b'degas'
    proof=json.loads((OUT/'hive-proof.json').read_text());hive=OUT/'SYSTEM.finish'
    assert sha(hive)==proof['candidate_sha256'];verify_gpt()
    shell('/data/local/tmp/degas-install/ntfs-3g -o rw,norecover,big_writes /dev/block/sdc96 '+M)
    files={};backup=M+'/DegasInstall/finish-backup-20261027'
    try:
        base=M+'/Windows/System32/config/';target=base+'SYSTEM'
        assert remote_sha(target)==proof['source_sha256']
        shell('test ! -e '+backup+' && mkdir '+backup)
        for name in ['SYSTEM','SOFTWARE','SAM','SYSTEM.LOG1','SYSTEM.LOG2','SOFTWARE.LOG1','SOFTWARE.LOG2','SAM.LOG1','SAM.LOG2']:
            if shell('if test -f '+base+name+'; then echo YES; fi').strip()!=b'YES':continue
            old=remote_sha(base+name);shell('cp '+base+name+' '+backup+'/'+name);assert remote_sha(backup+'/'+name)==old
        folder=M+'/Windows/Setup/Scripts';shell('mkdir -p '+folder)
        for name in ['DegasFinish.exe','DegasFinish.cmd','degas-finish-authorized.txt']:
            push(OUT/name,folder+'/'+name);files[name]=sha(OUT/name)
        push(hive,target+'.finish-new')
        # The captured original hive is clean (equal sequence numbers). Its old
        # transaction logs must not replay over the deliberately committed copy.
        for name in ['SYSTEM.LOG1','SYSTEM.LOG2']:
            shell('if test -f '+base+name+'; then mv '+base+name+' '+backup+'/'+name+'.moved; fi')
        shell('sync && mv -f '+target+'.finish-new '+target+' && sync')
        assert remote_sha(target)==proof['candidate_sha256']
    finally:shell('umount '+M)
    shell('/data/local/tmp/degas-install/ntfs-3g -o ro,norecover /dev/block/sdc96 '+M)
    try:
        assert remote_sha(M+'/Windows/System32/config/SYSTEM')==proof['candidate_sha256']
        for name,digest in files.items():assert remote_sha(M+'/Windows/Setup/Scripts/'+name)==digest
    finally:shell('umount '+M)
    verify_gpt();(OUT/'applied.json').write_text(json.dumps(dict(files=files,hive=proof,backup=backup,readback='passed',gpt_unchanged=True,sam_edited_offline=False,helper_run=False),indent=2))
    print('One-shot setup completion staged; backups/readback verified; helper not yet run')
if __name__=='__main__':main()
