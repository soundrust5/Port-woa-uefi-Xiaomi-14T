"""Capture active boot files read-only from rooted Android; never reboot."""
from pathlib import Path
import hashlib, json, sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from degas_full_windows_stage_source import shell

out = ROOT / 'evidence/raw/2026-10-29-degas-active-boot-identity'
out.mkdir(exist_ok=False)
assert shell('getprop ro.product.device').strip() == b'degas'
assert shell('id -u').strip() == b'0'
mounts = shell('cat /proc/mounts').decode()
out.joinpath('mounts-before.txt').write_text(mounts)
esp = '/data/local/tmp/degas-audit-esp'
win = '/data/local/tmp/degas-audit-win'
assert esp not in mounts and win not in mounts
shell('mkdir -p ' + esp + ' ' + win)
records = {}
def capture(src, dest):
    data = shell('cat ' + src)
    out.joinpath(dest).write_bytes(data)
    records[dest] = dict(source=src, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())

esp_mounted = win_mounted = False
try:
    shell('mount -t vfat -o ro /dev/block/sdc94 ' + esp)
    esp_mounted = True
    shell('/data/local/tmp/degas-install/ntfs-3g -o ro,norecover /dev/block/sdc96 ' + win)
    win_mounted = True
    capture(esp + '/EFI/Microsoft/Boot/BCD', 'BCD')
    for name in ['ntoskrnl.exe', 'winload.efi']:
        capture(win + '/Windows/System32/' + name, name)
    for remote, local in [
        (esp + '/EFI/Microsoft/Boot/bootmgfw.efi', 'bootmgfw.efi'),
        (win + '/Windows/Temp/DegasRuntime.log', 'DegasRuntime.log'),
    ]:
        if shell('if test -f ' + remote + '; then echo yes; fi').strip() == b'yes':
            capture(remote, local)
    out.joinpath('manifest.json').write_text(json.dumps(records, indent=2))
finally:
    if win_mounted:
        shell('umount ' + win)
    if esp_mounted:
        shell('umount ' + esp)
    out.joinpath('mounts-after.txt').write_bytes(shell('cat /proc/mounts'))
print(json.dumps(records, indent=2))
