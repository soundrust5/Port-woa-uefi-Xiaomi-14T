"""Flash one locally qualified boot_b profile from the expected Android boot."""
from pathlib import Path
import datetime, hashlib, json, shutil, subprocess, sys, time, zipfile
from degas_full_windows_stage_source import shell

root=Path(__file__).resolve().parents[1]
out=Path(sys.argv[1]).resolve();out.relative_to(root/'artifacts')
cfg=json.loads((out/'build-config.json').read_text())
pre=json.loads((out/'phone-preflight.json').read_text())
m=json.loads((out/'source-manifest.json').read_text());image=out/'boot.img'
assert hashlib.sha256(image.read_bytes()).hexdigest()==cfg['boot_image_sha256']
for p,h in m['sources'].items():assert hashlib.sha256((root/p).read_bytes()).hexdigest()==h,p
assert shell('getprop ro.product.device').strip()==b'degas'
assert shell('getprop ro.boot.slot_suffix').strip()==b'_b'
assert shell('sha256sum /dev/block/by-name/boot_b').decode().split()[0]==pre['current_boot_sha256']
if cfg.get('dvfs_control_writes'):
    thermal=json.loads((out/'thermal-preflash.json').read_text())
    age=datetime.datetime.now(datetime.timezone.utc)-datetime.datetime.fromisoformat(thermal['captured_utc'])
    assert 0<=age.total_seconds()<120 and thermal['battery_tenths_c']<400 and thermal['maximum_cpu_soc_millic']<60000
for n in ['DegasCheckpointDxe.efi','DegasAcpiDxe.efi','DegasSmbiosDxe.efi']:
    shutil.copy2(root/'mu/Build/degasPkg/DEBUG_CLANGPDB/AARCH64'/n,out/n)
shutil.copy2(root/'evidence/build/degas-first-dxe.log',out/'build.log')
with zipfile.ZipFile(out/'qualified-sources.zip','x',compression=zipfile.ZIP_DEFLATED) as z:
    for p in list(m['sources'])+['mu/Platforms/Xiaomi/degasPkg/Include/NativeContextIdentity.h']:z.write(root/p,p)
    for n in ['source-manifest.json','build-config.json']:z.write(out/n,n)
serial='XC7TUCXG95U8IVMN';fb=['C:/adb/fastboot.exe','-s',serial]
record=dict(build_id=cfg['build_id'],boot_image_sha256=cfg['boot_image_sha256'],target='boot_b',
    started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),early_firmware_written=False,
    windows_files_written=False,previous_boot_sha256=pre['current_boot_sha256'])
def save():(out/'flash-record.json').write_text(json.dumps(record,indent=2)+'\n')
save();subprocess.run(['C:/adb/adb.exe','-s',serial,'reboot','bootloader'],check=True,timeout=15)
for attempt in range(30):
    r=subprocess.run(['C:/adb/fastboot.exe','devices'],capture_output=True,text=True,timeout=5)
    if any(line.split()[:2]==[serial,'fastboot'] for line in r.stdout.splitlines()):break
    time.sleep(1)
else:raise RuntimeError('Expected fastboot device absent')
for name,value in [('product','degas'),('current-slot','b')]:
    r=subprocess.run(fb+['getvar',name],capture_output=True,text=True,timeout=10)
    assert r.returncode==0 and name+': '+value in r.stdout+r.stderr
r=subprocess.run(fb+['flash','boot_b',str(image)],capture_output=True,text=True,timeout=90)
record.update(flash_output=r.stdout+r.stderr,flash_completed=r.returncode==0);save()
assert r.returncode==0,record['flash_output']
r=subprocess.run(fb+['reboot'],capture_output=True,text=True,timeout=15);assert r.returncode==0
record.update(rebooted_to_uefi=True,completed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat());save()
print(record['flash_output']);print('PASS: qualified boot_b flashed; rebooted to UEFI')
