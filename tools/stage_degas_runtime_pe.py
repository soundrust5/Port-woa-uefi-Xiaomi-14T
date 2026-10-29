"""Stage diagnostics-only PE on ESP. No Windows NTFS mount or writes."""
from pathlib import Path
import subprocess,json,hashlib,shlex
from degas_full_windows_stage_source import shell,ADB
from stage_degas_ufs_winpe import verify_gpt
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'artifacts/degas/runtime-maintenance';E='/data/local/tmp/degas-installer-esp'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def rsha(p):return shell('sha256sum '+shlex.quote(p)).decode().split()[0]
pkg=json.loads((OUT/'package.json').read_text());wim=Path(pkg['wim'])
assert pkg['readback']=='passed' and not pkg['raw_journal'] and not pkg['image_apply'] and sha(wim)==pkg['wim_sha256']
assert shell('getprop ro.product.device').strip()==b'degas';verify_gpt()
temp='/data/local/tmp/degas-runtime-maintenance.wim'
subprocess.run(ADB+['push','-Z',str(wim),temp],check=True);assert rsha(temp)==pkg['wim_sha256']
shell('mount -t vfat -o rw /dev/block/sdc94 '+E)
try:
 target=E+'/sources/boot.wim';bcd=E+'/EFI/Microsoft/Boot/BCD'
 assert rsha(target)==pkg['source_sha256']
 assert rsha(bcd)==pkg['files']['BCD.runtime-original']
 # Free only a known duplicate backup whose exact bytes are preserved on PC.
 duplicate=E+'/sources/boot.wim.pre-maintenance'
 previous=json.loads((ROOT/'artifacts/degas/full-windows/install-pe/package.json').read_text())
 saved=Path(previous['candidate']);assert sha(saved)==previous['candidate_sha256'] and rsha(duplicate)==sha(saved)
 shell('rm '+shlex.quote(duplicate)+' && sync')
 free=int(shell('df -k '+E).decode().splitlines()[-1].split()[3])*1024
 assert free>wim.stat().st_size+8*1024*1024
 shell('cp '+temp+' '+target+'.runtime-new && sync');assert rsha(target+'.runtime-new')==pkg['wim_sha256']
 # Old active WIM is saved by matching source_sha256 in the host package.
 shell('mv -f '+target+'.runtime-new '+target+' && sync');assert rsha(target)==pkg['wim_sha256']
 subprocess.run(ADB+['push',str(OUT/'BCD.runtime-pe'),'/data/local/tmp/degas-runtime-bcd'],check=True)
 assert rsha('/data/local/tmp/degas-runtime-bcd')==pkg['bcd_pe_sha256']
 shell('cp /data/local/tmp/degas-runtime-bcd '+bcd+'.runtime-new && sync')
 assert rsha(bcd+'.runtime-new')==pkg['bcd_pe_sha256']
 shell('mv -f '+bcd+'.runtime-new '+bcd+' && sync');assert rsha(bcd)==pkg['bcd_pe_sha256']
finally:shell('umount '+E)
verify_gpt();(OUT/'staged.json').write_text(json.dumps(dict(wim_sha256=pkg['wim_sha256'],bcd_sha256=pkg['bcd_pe_sha256'],duplicate_backup_saved_on_pc=str(saved),ntfs_written=False,gpt_unchanged=True,readback='passed'),indent=2))
print('PASS: diagnostics PE staged on ESP only; Windows NTFS unchanged')
