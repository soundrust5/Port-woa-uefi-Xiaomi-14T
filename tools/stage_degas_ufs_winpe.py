"""Update only verified ESP files. No formatting/GPT/Android modifications."""
from pathlib import Path
import argparse,hashlib,json,subprocess,datetime,struct
from collect_degas_windows_ufs_baseline import read_shell as shell,ADB
ROOT=Path(__file__).resolve().parents[1]
PLAN=ROOT/'evidence/raw/2026-10-25-degas-windows-partition-plan'
OUT=ROOT/'artifacts/degas/ufs-windows'
MOUNT='/data/local/tmp/degas-ufs-esp'
BASE_WIM='2490c9c9afdb6e7c3e54ec5653f69b8be3465a2f0e178bf475daf3c005c5a5fc'
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def digest_remote(p):return shell('sha256sum '+p).decode().split()[0]
def verify_gpt():
 identity=ROOT/'artifacts/degas/full-windows/gpt-identity.json'
 headers=json.loads(identity.read_text())['headers'] if identity.exists() else {}
 for lba,count,name in [(1,1,'new-primary.bin'),(2,4,'new-entries.bin'),(64577535,1,'new-backup.bin'),(64577528,4,'new-entries.bin')]:
  expected=(PLAN/name).read_bytes()
  if name in headers:
   expected=Path(headers[name]['path']).read_bytes()
   assert hashlib.sha256(expected).hexdigest()==headers[name]['sha256']
  assert shell(f'dd if=/dev/block/sdc bs=4096 skip={lba} count={count} 2>/dev/null')==expected,name
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--update',action='store_true');args=ap.parse_args()
 assert shell('getprop ro.product.device').strip()==b'degas'
 assert shell('getprop ro.boot.slot_suffix').strip()==b'_b'
 assert shell('getprop sys.boot_completed').strip()==b'1'
 assert shell('readlink -f /dev/block/by-name/esp').strip()==b'/dev/block/sdc94'
 assert int(shell('blockdev --getsize64 /dev/block/sdc94'))==2147483648
 assert int(shell('cat /sys/class/block/sdc94/start'))==29267968*8
 verify_gpt()
 assert shell('dd if=/dev/block/sdc96 bs=4096 count=1 2>/dev/null')[3:11]!=b'NTFS    ','Raw-journal WinPE must never be staged over populated NTFS Windows'
 package=json.loads((ROOT/'artifacts/degas/winpe-log/package.json').read_text())
 driver=json.loads((OUT/'package.json').read_text())
 bcd=json.loads((OUT/'bcd/manifest.json').read_text())
 wim=Path(package['candidate']);new_bcd=OUT/'bcd/BCD.test'
 assert sha(wim)==package['candidate_sha256'] and package['wim_verify']=='passed'
 assert package['driver_files_staged_only']==driver['files']
 assert driver['read_only'] or (driver.get('write_test_lun')==2 and driver.get('write_test_lba')==29800448 and driver.get('write_test_bytes')==268435456 and driver.get('automatic_watchdog') is False)
 if not driver['read_only']:
  baseline=json.loads((ROOT/'evidence/raw/2026-10-27-degas-ufs-stress-baseline/manifest.json').read_text())
  region=shell('dd if=/dev/block/sdc bs=4096 skip=29800448 count=65536 2>/dev/null')
  assert len(region)==268435456,'short scratch read'
  current=hashlib.sha256(region).hexdigest()
  accepted={baseline['region_sha256']}
  # V32 independently verified full-region pattern. Original backup remains
  # intact. Accept only this exact post-stress state for the first journal.
  if package.get('journal_mode')=='raw-two-slot-v1':
   accepted.add('a1054f8d5312a0852c315f7f90f64a5ad89dd4f2259fee88687b1b42594a32c2')
  if current not in accepted and package.get('journal_mode')=='raw-two-slot-v1':
   # Later journal sessions legitimately replace only the first128KiB.
   # Require both committed CRC-valid slots and the EXACT qualified stress
   # pattern everywhere else; unknown scratch changes still stop deployment.
   from read_degas_ufs_journal import decode
   slots=region[:131072];records=decode(slots)
   assert len(records)==2,'need two committed journal slots before update'
   expected=hashlib.sha256()
   pattern=bytearray(((i*73+41)^(i>>4))&255 for i in range(4096))
   for lba in range(29800480,29800448+65536):
    struct.pack_into('<II',pattern,0,lba,(~lba)&0xffffffff)
    expected.update(pattern)
   assert hashlib.sha256(region[131072:]).digest()==expected.digest(),'non-journal scratch blocks changed'
   preserved=ROOT/'evidence/raw'/('degas-prestage-journal-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
   preserved.mkdir(exist_ok=False);(preserved/'slots.bin').write_bytes(slots)
   for r in records:(preserved/f"slot-{r['slot']}.txt").write_bytes(r.pop('payload'))
   (preserved/'manifest.json').write_text(json.dumps({'region_sha256':current,'records':records,'tail_pattern_verified':True},indent=2))
   accepted.add(current)
  assert current in accepted,'scratch extent changed: preserve/read back before deployment'
 assert sha(new_bcd)==bcd['candidate_sha256'] and bcd['semantic_comparison']=='passed'
 previous=json.loads((OUT/'staged.json').read_text()) if (OUT/'staged.json').exists() else None
 assert bool(previous)==args.update,'Use --update only for a verified previous deployment'
 assert b'/dev/block/sdc94 ' not in shell('cat /proc/mounts')
 shell('mkdir -p '+MOUNT)
 shell('mount -t vfat -o rw,uid=2000,gid=2000,umask=0077 /dev/block/sdc94 '+MOUNT)
 try:
  old_wim=MOUNT+'/sources/boot.wim';old_bcd=MOUNT+'/EFI/Microsoft/Boot/BCD'
  assert digest_remote(old_wim)==(previous['wim_sha256'] if previous else BASE_WIM)
  assert digest_remote(old_bcd)==(previous['bcd_sha256'] if previous else bcd['source_sha256'])
  if previous:
   assert digest_remote(old_wim+'.before-degasufs')==BASE_WIM
   assert digest_remote(old_bcd+'.before-degasufs')==bcd['source_sha256']
  for src,dst,expected in [(wim,old_wim+'.degas-new',package['candidate_sha256']),(new_bcd,old_bcd+'.degas-new',bcd['candidate_sha256'])]:
   subprocess.run(ADB+['push',str(src),dst],check=True,timeout=180)
   assert digest_remote(dst)==expected
  shell('sync')
  if previous:
   shell('mv -f '+old_wim+'.degas-new '+old_wim)
  else:
   shell('mv '+old_wim+' '+old_wim+'.before-degasufs')
   try:shell('mv '+old_wim+'.degas-new '+old_wim)
   except BaseException:
    shell('mv '+old_wim+'.before-degasufs '+old_wim);raise
   shell('cp '+old_bcd+' '+old_bcd+'.before-degasufs')
  shell('mv '+old_bcd+'.degas-new '+old_bcd)
  shell('sync')
  assert digest_remote(old_wim)==package['candidate_sha256']
  assert digest_remote(old_bcd)==bcd['candidate_sha256']
  assert digest_remote(old_wim+'.before-degasufs')==BASE_WIM
  assert digest_remote(old_bcd+'.before-degasufs')==bcd['source_sha256']
 finally:
  shell('sync');shell('umount '+MOUNT)
 verify_gpt()
 report=dict(wim_sha256=package['candidate_sha256'],bcd_sha256=bcd['candidate_sha256'],
  backups_retained=True,readback='passed',gpt_unchanged=True,phone='degas',serial=ADB[-1])
 if previous:
  stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d-%H%M%S')
  (OUT/('staged-before-'+stamp+'.json')).write_text(json.dumps(previous,indent=2))
 (OUT/'staged.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
if __name__=='__main__':main()
