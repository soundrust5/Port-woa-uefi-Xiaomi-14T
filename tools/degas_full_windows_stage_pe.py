"""Replace only ESP boot.wim with verified installer PE without raw journal."""
from pathlib import Path
import hashlib,json,subprocess
from degas_full_windows_stage_source import shell,ADB
from stage_degas_ufs_winpe import verify_gpt
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/degas/full-windows'
MOUNT='/data/local/tmp/degas-installer-esp'
def digest(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def remote_sha(p):return shell('sha256sum '+p).decode().split()[0]
def main():
 assert shell('getprop ro.product.device').strip()==b'degas'
 assert shell('getprop ro.boot.slot_suffix').strip()==b'_b'
 assert shell('getprop sys.boot_completed').strip()==b'1'
 assert shell('readlink -f /dev/block/by-name/esp').strip()==b'/dev/block/sdc94'
 assert int(shell('blockdev --getsize64 /dev/block/sdc94'))==2147483648
 verify_gpt()
 package=json.loads((OUT/'install-pe/package.json').read_text())
 assert package['raw_journal_disabled'] and package['wim_verify']=='passed'
 wim=Path(package['candidate']);assert digest(wim)==package['candidate_sha256']
 old=json.loads((ROOT/'artifacts/degas/ufs-windows/staged.json').read_text())
 assert b'/dev/block/sdc94 ' not in shell('cat /proc/mounts')
 shell('mkdir -p '+MOUNT+' && mount -t vfat -o rw,uid=2000,gid=2000,umask=0077 /dev/block/sdc94 '+MOUNT)
 try:
  target=MOUNT+'/sources/boot.wim';bcd=MOUNT+'/EFI/Microsoft/Boot/BCD'
  assert remote_sha(target)==old['wim_sha256']
  assert remote_sha(bcd)==old['bcd_sha256']
  subprocess.run(ADB+['push',str(wim),target+'.install-new'],check=True,timeout=180)
  assert remote_sha(target+'.install-new')==package['candidate_sha256']
  shell('sync && mv -f '+target+'.install-new '+target+' && sync')
  assert remote_sha(target)==package['candidate_sha256']
  assert remote_sha(bcd)==old['bcd_sha256']
 finally:shell('umount '+MOUNT)
 verify_gpt()
 (OUT/'installer-staged.json').write_text(json.dumps({'wim_sha256':package['candidate_sha256'],'bcd_sha256':old['bcd_sha256'],'previous_test':old,'readback':'passed','raw_journal_disabled':True,'gpt_unchanged':True},indent=2))
 print('PASS: installer PE staged; raw journal disabled; BCD unchanged')
if __name__=='__main__':main()
