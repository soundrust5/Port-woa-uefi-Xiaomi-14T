"""Verify the built diagnostic EFI, then package it without phone I/O."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--kind',choices=['identity','bugcheck'],default='identity')
ap.add_argument('--version',type=int,default=1)
args=ap.parse_args()
monitor=args.kind=='bugcheck'
assert args.version>0
stem=('degas-bugcheck-monitor-v' if monitor else 'degas-two-cpu-runtime-id-v')+str(args.version)
OUT=ROOT/'artifacts'/stem
def sha(b):return hashlib.sha256(b).hexdigest()
manifest=json.loads((OUT/'source-manifest.json').read_text())
assert all(sha((ROOT/p).read_bytes())==digest for p,digest in manifest['sources'].items())
efi=(ROOT/'mu/Build/degasPkg/DEBUG_CLANGPDB/AARCH64/DegasAcpiDxe.efi').read_bytes()
code=(ROOT/'artifacts/degas/cpu-audit'/('windows-bugcheck.bin' if monitor else 'windows-identity.bin')).read_bytes()
assert efi.count(code)==1,'Linked EFI does not contain the tested identity reader'
if monitor:
    parking=(ROOT/'artifacts/degas/cpu-audit/parking-bugcheck.bin').read_bytes()
    assert efi.count(parking)==1,'Linked Parking differs from the emulated fixture'
assert sha((ROOT/'diagnostic/build-mu-handoff/entry.bin').read_bytes())=='011034ec4bd9e6b81505603894940358137a7c707028fc825afcc48235bc59a3'
assert sha((ROOT/'artifacts/degas-prime-fullscreen-v1/boot.img').read_bytes())=='d4297ff9f41b894087ec259ecfa5bf141404bb0ee43fcc950bbb85213291485e'
log=(ROOT/'evidence/build/degas-first-dxe.log').read_text(errors='replace')
assert '\n- Done -\n' in log and '\n- Failed -\n' not in log
assert all(text in log for text in [f'-DDEGAS_CPU_TEST_LIMIT={3 if monitor else 2}','-DDEGAS_BOOT_CORE=0','-DDEGAS_WINDOWS_IDENTITY_OBSERVER=1'])
if monitor:assert '-DDEGAS_WINDOWS_BUGCHECK_MONITOR=1' in log
config=dict(boot_core=0,cpu_test_limit=3 if monitor else 2,parking=1,runtime_identity=1,bugcheck_monitor=monitor,ebs_watchdog_seconds=30,
            observer_bytes=len(code),observer_sha256=sha(code),observer_exact_linked_match=True,
            acpi_efi_sha256=sha(efi),source_manifest_sha256=manifest['source_manifest_sha256'])
(OUT/'build-config.json').write_text(json.dumps(config,indent=2))
def run(*args):subprocess.run([sys.executable,*args],cwd=ROOT,check=True)
run('tools/package_degas_first_dxe.py',stem+'-intermediate')
run('tools/package_degas_dualboot.py','artifacts/'+stem+'-intermediate/kernel.bin.gz',str(OUT),'--boot-core','0')
run('tools/package_degas_boot.py','--kernel',str(OUT/'kernel.bin.gz'),
    '--stock','evidence/raw/2026-10-24-degas-return/boot_b.img',
    '--output',str(OUT/'boot-uefi.img'),'--manifest',str(OUT/'boot-manifest.json'),
    '--restore-reference',str(OUT/'restore.json'))
run('tools/package_degas_stock_overlay.py',str(OUT/'boot-uefi.img'),str(OUT/'boot.img'))
print('Verified diagnostic image:',sha((OUT/'boot.img').read_bytes()))
