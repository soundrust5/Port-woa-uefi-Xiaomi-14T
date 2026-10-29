"""Stage verified clean Windows source to Android /data only. No partitions touched."""
from pathlib import Path
import hashlib,json,shlex,subprocess,uuid
from collect_probe_ramoops import unpack_capture

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'artifacts/oneplus12/full-windows/install-pro.wim'
SHA='4400c3d350b71fbbf09d9e700c8ff9b1264d92707400e8d8fbff7ce47e5d424e'
ADB=['C:/adb/adb.exe','-s','XC7TUCXG95U8IVMN']
DEST='/data/local/tmp/degas-install/install-pro.wim'

def shell(command):
 token='DEGAS_STAGE_'+uuid.uuid4().hex
 wrapped='('+command+'); task_rc=$?; printf "\\n'+token+':%s\\n" "$task_rc"'
 r=subprocess.run(ADB+['exec-out','su -c '+shlex.quote(wrapped)],capture_output=True,timeout=1200)
 rc,data=unpack_capture(r.stdout,token)
 if r.returncode or rc:raise RuntimeError((command,r.returncode,rc,r.stderr))
 return data

def main():
 with SOURCE.open('rb') as f:assert hashlib.file_digest(f,'sha256').hexdigest()==SHA
 assert SOURCE.stat().st_size==6871685563
 assert shell('getprop ro.product.device').strip()==b'degas'
 assert shell('id -u').strip()==b'0'
 assert shell('getprop sys.boot_completed').strip()==b'1'
 free=int(shell('df -k /data').decode().splitlines()[-1].split()[3])*1024
 assert free>SOURCE.stat().st_size+2*1024**3,'not enough Android staging space'
 shell('mkdir -p /data/local/tmp/degas-install && test ! -L /data/local/tmp/degas-install && chown 2000:2000 /data/local/tmp/degas-install')
 current=shell('if test -f '+DEST+'; then sha256sum '+DEST+'; fi').decode().split()
 if not current or current[0]!=SHA:
  subprocess.run(ADB+['push','-Z',str(SOURCE),DEST+'.new'],check=True,timeout=1200)
  assert shell('sha256sum '+DEST+'.new').decode().split()[0]==SHA
  shell('mv -f '+DEST+'.new '+DEST+' && sync')
 assert shell('sha256sum '+DEST).decode().split()[0]==SHA
 out=ROOT/'artifacts/degas/full-windows';out.mkdir(parents=True,exist_ok=True)
 (out/'android-source-staged.json').write_text(json.dumps(dict(source=str(SOURCE),sha256=SHA,bytes=SOURCE.stat().st_size,
  destination=DEST,phone='degas',serial=ADB[2],readback='passed',partition_formatted=False,windows_installed=False),indent=2))
 print('PASS: clean Windows source verified in Android /data only')

if __name__=='__main__':main()
