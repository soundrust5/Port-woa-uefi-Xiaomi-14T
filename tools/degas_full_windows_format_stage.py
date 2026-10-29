"""One-time authorized format of captured Windows partition, then NTFS inputs.
Requires installer PE already deployed and verified: never boot raw-journal PE
after NTFS exists. No GPT, Android, userdata, boot or BCD writes.
"""
from pathlib import Path
import hashlib,json,subprocess,datetime
from degas_full_windows_stage_source import shell,ADB,SHA
from stage_degas_ufs_winpe import verify_gpt
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/degas/full-windows'
REMOTE='/data/local/tmp/degas-install'
MOUNT='/data/local/tmp/degas-windows-ntfs'
ESP='/data/local/tmp/degas-install-esp-check'
def digest(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def remote_sha(p):return shell('sha256sum '+p).decode().split()[0]
def main():
 assert shell('getprop ro.product.device').strip()==b'degas'
 assert shell('getprop ro.boot.slot_suffix').strip()==b'_b'
 assert shell('getprop sys.boot_completed').strip()==b'1'
 assert shell('readlink -f /dev/block/by-name/Windows').strip()==b'/dev/block/sdc96'
 assert int(shell('blockdev --getsize64 /dev/block/sdc96'))==142446919680
 assert int(shell('cat /sys/class/block/sdc96/start'))==29796352*8
 assert int(shell('blockdev --getss /dev/block/sdc96'))==4096
 verify_gpt()
 staged=json.loads((OUT/'installer-staged.json').read_text())
 assert staged['raw_journal_disabled'] and staged['readback']=='passed'
 mounts=shell('cat /proc/mounts')
 assert b'/dev/block/sdc96 ' not in mounts and b'/dev/block/sdc94 ' not in mounts
 shell('mkdir -p '+ESP+' && mount -t vfat -o ro /dev/block/sdc94 '+ESP)
 try:assert remote_sha(ESP+'/sources/boot.wim')==staged['wim_sha256']
 finally:shell('umount '+ESP)
 assert remote_sha(REMOTE+'/install-pro.wim')==SHA
 assert remote_sha(REMOTE+'/mkntfs')=='3d56bf7d2c06f6323275fdd7c335b82905036abb574289a0cdfeecef06505dc1'
 assert remote_sha(REMOTE+'/ntfs-3g')=='77f47b1b5655a385c136d1b6bc0b0e1cd15f43446bb89886aa7b6c7a34b29a15'
 before=shell('dd if=/dev/block/sdc96 bs=4096 count=256 2>/dev/null')
 assert len(before)==1048576 and before[3:11]!=b'NTFS    ','Refuse to format existing NTFS'
 evidence=ROOT/'evidence/raw'/('degas-windows-format-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
 evidence.mkdir(exist_ok=False);(evidence/'first1MiB.before.bin').write_bytes(before)
 guard_before=shell('dd if=/dev/block/sdc bs=4096 skip=29796351 count=1 2>/dev/null')
 guard_after=shell('dd if=/dev/block/sdc bs=4096 skip=64573432 count=1 2>/dev/null')
 (evidence/'before-guard.bin').write_bytes(guard_before);(evidence/'after-guard.bin').write_bytes(guard_after)
 result=shell(REMOTE+'/mkntfs -Q -L Windows -s 4096 -c 4096 -p 29796352 -H 255 -S 63 /dev/block/sdc96 34777080 2>&1')
 (evidence/'mkntfs.txt').write_bytes(result);print(result.decode(errors='replace'),flush=True)
 shell('sync');verify_gpt()
 assert shell('dd if=/dev/block/sdc bs=4096 skip=29796351 count=1 2>/dev/null')==guard_before
 assert shell('dd if=/dev/block/sdc bs=4096 skip=64573432 count=1 2>/dev/null')==guard_after
 boot=shell('dd if=/dev/block/sdc96 bs=4096 count=1 2>/dev/null');assert boot[3:11]==b'NTFS    '
 (evidence/'ntfs-boot.bin').write_bytes(boot)
 (OUT/'formatted.json').write_text(json.dumps({'partition':'/dev/block/sdc96','bytes':142446919680,'gpt_unchanged':True,'guards_unchanged':True,'raw_journal_disabled':True,'evidence':str(evidence)},indent=2))
 stage()
def stage():
 # Called only following the verified one-time format above.
 payload=OUT/'phone-payload';assert (payload/'install.cmd').is_file()
 shell('mkdir -p '+MOUNT)
 shell(REMOTE+'/ntfs-3g -o rw,norecover,big_writes /dev/block/sdc96 '+MOUNT)
 try:
  rows=[x.split() for x in shell('cat /proc/mounts').decode().splitlines() if x.split()[1]==MOUNT]
  assert len(rows)==1 and rows[0][0]=='/dev/block/sdc96' and 'rw' in rows[0][3].split(',')
  shell('test ! -e '+MOUNT+'/Windows && mkdir -p '+MOUNT+'/DegasInstall')
  subprocess.run(ADB+['push',str(payload)+'/.',REMOTE+'/payload'],check=True,timeout=180)
  files={str(p.relative_to(payload)).replace('\\','/'):digest(p) for p in payload.rglob('*') if p.is_file()}
  for name,sha in files.items():assert remote_sha(REMOTE+'/payload/'+name)==sha
  shell('cp -R '+REMOTE+'/payload/. '+MOUNT+'/DegasInstall/')
  shell('cp '+REMOTE+'/install-pro.wim '+MOUNT+'/DegasInstall/install-pro.wim && sync')
  assert remote_sha(MOUNT+'/DegasInstall/install-pro.wim')==SHA
  for name,sha in files.items():assert remote_sha(MOUNT+'/DegasInstall/'+name)==sha
  shell('sync')
 finally:shell('umount '+MOUNT)
 verify_gpt()
 (OUT/'ntfs-staged.json').write_text(json.dumps({'files':files,'source_sha256':SHA,'readback':'passed','unmounted':True,'windows_installed':False},indent=2))
 print('PASS: NTFS staged and unmounted; Windows not yet applied')
if __name__=='__main__':main()
