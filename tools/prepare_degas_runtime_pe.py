"""Prepare file-only WinPE diagnostics; no phone writes or host registry loads."""
from pathlib import Path
import hashlib,json,shutil,subprocess,sys
from degas_full_windows_stage_source import shell
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'artifacts/degas/runtime-maintenance';ESP='/data/local/tmp/degas-installer-esp'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
source=json.loads((ROOT/'artifacts/degas/setup-maintenance/pe-package.json').read_text());original=Path(source['wim']);assert sha(original)==source['sha256']
shell('mount -t vfat -o ro /dev/block/sdc94 '+ESP)
try:(OUT/'BCD.runtime-original').write_bytes(shell('cat '+ESP+'/EFI/Microsoft/Boot/BCD'))
finally:shell('umount '+ESP)
# Isolated WSL helper edits only BootMgr default/timeout in this captured copy.
code='''import hivex,struct,sys
h=hivex.Hivex(sys.argv[1],write=True)
n=h.node_get_child(h.node_get_child(h.root(),'Objects'),'{9dea862c-5cdd-4e70-acc1-f32b344d4795}')
n=h.node_get_child(n,'Elements')
for key,t,value in [('23000003',1,('{7619dcc9-fafe-11d9-b411-000476eba25f}'+chr(0)).encode('utf-16le')),('25000004',3,struct.pack('<Q',0))]:
 e=h.node_get_child(n,key);assert e;h.node_set_value(e,{'key':'Element','t':t,'value':value})
h.commit(sys.argv[2])
'''
(OUT/'prepare_bcd.py').write_text(code)
subprocess.run(['wsl','-d','Ubuntu','--','python3','artifacts/degas/runtime-maintenance/prepare_bcd.py','artifacts/degas/runtime-maintenance/BCD.runtime-original','artifacts/degas/runtime-maintenance/BCD.runtime-pe'],check=True)
wim=OUT/'boot-runtime.wim';assert not wim.exists();shutil.copyfile(original,wim)
shutil.copyfile(ROOT/'artifacts/degas/runtime-log/DegasRuntime.exe',OUT/'DegasRuntime.exe')
shutil.copyfile(ROOT/'tools/degas_runtime_maintenance.cmd',OUT/'degas-runtime-maintenance.cmd')
files=['degas-install-bootstrap.exe','DegasRuntime.exe','degas-runtime-maintenance.cmd','BCD.runtime-original']
commands=''.join('add "'+str(OUT/n)+'" "/Windows/System32/'+n+'"\n' for n in files)
assert b'DGSLOG01' not in (OUT/'degas-install-bootstrap.exe').read_bytes()
subprocess.run(['wimlib-imagex','update',str(wim),'2','--check'],input=commands,text=True,check=True,stdout=subprocess.DEVNULL)
subprocess.run(['wimlib-imagex','verify',str(wim)],check=True,stdout=subprocess.DEVNULL)
rb=OUT/'readback';rb.mkdir(exist_ok=True)
subprocess.run(['wimlib-imagex','extract',str(wim),'2',*[f'Windows/System32/{n}' for n in files],'--dest-dir='+str(rb)],check=True,stdout=subprocess.DEVNULL)
for n in files:assert sha(rb/n)==sha(OUT/n)
(OUT/'package.json').write_text(json.dumps(dict(wim=str(wim),wim_sha256=sha(wim),source_sha256=sha(original),files={n:sha(OUT/n) for n in files},bcd_pe_sha256=sha(OUT/'BCD.runtime-pe'),readback='passed',image_apply=False,format=False,raw_journal=False),indent=2))
print('PASS: diagnostics-only PE verified; no phone files changed')
