"""Dedicated raw-journal-free installer PE; no phone/host registry writes."""
from pathlib import Path
import datetime,json,shutil,stat,subprocess
from package_degas_winpe_log import ROOT,SOURCE,BASE_HASH,digest,arm64_pe,helper

OUT=ROOT/'artifacts/degas/install-pe'
UFS=ROOT/'artifacts/degas/ufs-windows/install-candidate/test-package'
TOUCH=ROOT/'artifacts/degas/touch-windows/test-package'

def main():
 assert digest(SOURCE)==BASE_HASH
 exe=OUT/'degas-install-bootstrap.exe';arm64_pe(exe)
 # Do not package any executable capable of the obsolete raw scratch journal.
 assert b'DGSLOG01' not in exe.read_bytes()
 stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d-%H%M%S')
 work=OUT/('package-'+stamp);work.mkdir(parents=True,exist_ok=False)
 (work/'winpeshl.ini').write_bytes(b'[LaunchApp]\r\nAppPath = %SYSTEMROOT%\\System32\\degas-install-bootstrap.exe\r\n')
 (work/'degas-install-authorized.txt').write_text('Xiaomi degas exact Windows/ESP partitions; no format in PE.\n')
 hive=work/'hive';hive.mkdir()
 subprocess.run(['wimlib-imagex','extract',str(SOURCE),'2','Windows/System32/config/SYSTEM',f'--dest-dir={hive}'],check=True)
 edited=hive/'SYSTEM-INSTALL';helper(hive/'SYSTEM',edited)
 entries=[(exe,'Windows/System32/degas-install-bootstrap.exe'),(work/'winpeshl.ini','Windows/System32/winpeshl.ini'),(work/'degas-install-authorized.txt','Windows/System32/degas-install-authorized.txt'),(edited,'Windows/System32/config/SYSTEM')]
 manifests={}
 for directory,name,target in [(UFS,'DegasUfs','ACPI\\DGS0001'),(TOUCH,'DegasTouch','ACPI\\DGS0002')]:
  manifest_path=directory.parent/'package.json' if name=='DegasUfs' else directory/'package.json'
  manifest=json.loads(manifest_path.read_text());assert manifest['target']==target
  files={p.name:digest(p) for p in directory.iterdir() if p.is_file() and p.name!='package.json'}
  assert files==manifest['files']
  assert files['development.cer']=='0515a9334e3dfc3c4a90a52dfc8a5fc6a0e995d199d77e92a1a90f3ef37920d6'
  assert all(Path(f).suffix.lower() in {'.inf','.cat','.sys','.cer'} for f in files)
  for f in files:
   p=directory/f
   if p.suffix.lower()=='.sys':arm64_pe(p)
   entries.append((p,'Windows/System32/'+name+'/'+f))
  manifests[name]=manifest
 assert manifests['DegasUfs'].get('install_mode') is True, 'must use scoped install candidate'
 candidate=OUT/('boot-degas-install-'+stamp+'.wim');shutil.copyfile(SOURCE,candidate);candidate.chmod(stat.S_IREAD|stat.S_IWRITE)
 commands=''.join(f'add "{p}" "/{target}"\n' for p,target in entries)
 (work/'update.txt').write_text(commands)
 subprocess.run(['wimlib-imagex','update',str(candidate),'2','--check','--threads=4'],input=commands,text=True,check=True)
 subprocess.run(['wimlib-imagex','verify',str(candidate)],check=True)
 checked={}
 for i,(p,target) in enumerate(entries):
  dest=work/('readback-'+str(i));dest.mkdir()
  subprocess.run(['wimlib-imagex','extract',str(candidate),'2',target,f'--dest-dir={dest}'],check=True)
  assert digest(dest/Path(target).name)==digest(p);checked[target]=digest(p)
 assert digest(SOURCE)==BASE_HASH
 report=dict(candidate=str(candidate),candidate_sha256=digest(candidate),source_sha256=BASE_HASH,raw_journal_disabled=True,
  startup='degas-install-bootstrap.exe',automatic_format=False,automatic_reboot=False,wim_verify='passed',readback=checked,
  driver_packages=manifests,phone_accessed=False,installed=False)
 (work/'manifest.json').write_text(json.dumps(report,indent=2));(OUT/'package.json').write_text(json.dumps(report,indent=2))
 shared=ROOT/'artifacts/degas/full-windows/install-pe';shared.mkdir(parents=True,exist_ok=True)
 (shared/'package.json').write_text(json.dumps(report,indent=2))
 print(json.dumps({k:report[k] for k in ['candidate','candidate_sha256','raw_journal_disabled','wim_verify']},indent=2))
if __name__=='__main__':main()
