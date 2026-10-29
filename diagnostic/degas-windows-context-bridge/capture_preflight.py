"""Read-only phone identity before the recoverable boot.img experiment."""
from pathlib import Path
import argparse, json, sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from degas_full_windows_stage_source import shell
from bridge_fixture import SOURCE_SHA256

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,default=ROOT/'artifacts/degas-native-context-v1')
parser.add_argument('--expected-current-boot-sha',default='fa01819342dd24164dea568f88f9715bc3132f738a68c49453907982946c1113')
args=parser.parse_args();out=args.output;out.mkdir(parents=True,exist_ok=True)
assert shell('getprop ro.product.device').strip() == b'degas'
assert shell('getprop ro.boot.slot_suffix').strip() == b'_b'
assert shell('getprop sys.boot_completed').strip() == b'1'
assert shell('id -u').strip() == b'0'
mount = '/data/local/tmp/degas-audit-win'
assert mount not in shell('cat /proc/mounts').decode()
assert shell('test -d ' + mount + ' && echo yes').strip() == b'yes'
boot_sha = shell('sha256sum /dev/block/by-name/boot_b').decode().split()[0]
assert boot_sha == args.expected_current_boot_sha
mounted = False
try:
    shell('/data/local/tmp/degas-install/ntfs-3g -o ro,norecover /dev/block/sdc96 ' + mount)
    mounted = True
    nt_sha = shell('sha256sum ' + mount + '/Windows/System32/ntoskrnl.exe').decode().split()[0]
    assert nt_sha == SOURCE_SHA256
    runtime = shell('if test -f ' + mount + '/Windows/Temp/DegasRuntime.log; then cat ' + mount + '/Windows/Temp/DegasRuntime.log; fi')
    out.joinpath('runtime-before.log').write_bytes(runtime)
finally:
    if mounted:
        shell('umount ' + mount)
assert mount not in shell('cat /proc/mounts').decode()
record = dict(product='degas', serial='XC7TUCXG95U8IVMN', active_slot='b',
              current_boot_sha256=boot_sha, installed_nt_sha256=nt_sha,
              filesystem_mounted_readonly=True, filesystem_unmounted=True,
              partitions_written=False, rebooted=False)
out.joinpath('phone-preflight.json').write_text(json.dumps(record, indent=2) + '\n')
print(json.dumps(record, indent=2))
