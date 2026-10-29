from pathlib import Path
import subprocess,hashlib,json
from degas_full_windows_stage_source import shell,ADB
from stage_degas_ufs_winpe import verify_gpt
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'artifacts/degas/runtime-log';M='/data/local/tmp/degas-windows-ntfs'
def sha(b):return hashlib.sha256(b).hexdigest()
def put(local,remote):
 temp='/data/local/tmp/degas-runtime-upload'
 subprocess.run(ADB+['push',str(local),temp],check=True)
 assert shell('sha256sum '+temp).decode().split()[0]==sha(local.read_bytes())
 shell('cp '+temp+' '+remote+' && sync')
 assert shell('sha256sum '+remote).decode().split()[0]==sha(local.read_bytes())
assert shell('getprop ro.product.device').strip()==b'degas';verify_gpt()
shell('/data/local/tmp/degas-install/ntfs-3g -o rw,norecover,big_writes /dev/block/sdc96 '+M)
try:
 startup=M+'/ProgramData/Microsoft/Windows/Start Menu/Programs/Startup'
 import shlex
 shell('test -d '+shlex.quote(startup))
 put(OUT/'DegasRuntime.exe',M+'/Windows/System32/DegasRuntime.exe')
 cmd=OUT/'DegasRuntime.cmd';cmd.write_bytes(b'@echo off\r\n"%SystemRoot%\\System32\\DegasRuntime.exe"\r\n')
 put(cmd,shlex.quote(startup+'/DegasRuntime.cmd'))
finally:shell('umount '+M)
verify_gpt();(OUT/'staged.json').write_text(json.dumps(dict(mode='common-user-startup',registry_modified=False,exe_sha256=sha((OUT/'DegasRuntime.exe').read_bytes()),log=r'C:\Windows\Temp\DegasRuntime.log'),indent=2))
print('Runtime startup staged; readback verified; registry and GPT unchanged')
