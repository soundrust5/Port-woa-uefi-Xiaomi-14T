"""Read-only, allowlisted rooted-ADB hardware survey. Writes evidence on the PC.

Does not reboot, flash, mount debugfs, read /dev/mem or modify Android settings.
Raw evidence may contain device identifiers; keep it local.
"""
import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
import subprocess


TEXT = {
    'identity.txt': 'id; uname -a; getprop; getconf PAGESIZE',
    'cpuinfo.txt': 'cat /proc/cpuinfo',
    'meminfo.txt': 'cat /proc/meminfo',
    'iomem.txt': 'cat /proc/iomem',
    'cmdline.txt': 'cat /proc/cmdline',
    'bootconfig.txt': 'cat /proc/bootconfig',
    'interrupts.txt': 'cat /proc/interrupts',
    'modules.txt': 'cat /proc/modules',
    'partitions.txt': 'cat /proc/partitions; ls -l /dev/block/by-name',
    'partition-geometry.txt': '''for p in /sys/class/block/sd*; do
echo "$p"; for a in start size partition; do
if [ -r "$p/$a" ]; then echo "$a"; cat "$p/$a"; fi; done; done''',
    'dmesg.txt': 'dmesg',
    'input.txt': 'cat /proc/bus/input/devices',
    'platform-devices.txt': 'ls -l /sys/bus/platform/devices',
    'i2c-spi.txt': '''for p in /sys/bus/i2c/devices/* /sys/bus/spi/devices/*; do
[ -e "$p" ] || continue; echo "$p"; readlink "$p/driver";
for a in name modalias; do if [ -r "$p/$a" ]; then cat "$p/$a"; fi; done; done''',
    'framebuffer.txt': '''ls -l /dev/graphics /sys/class/graphics;
for p in /sys/class/graphics/fb*; do [ -e "$p" ] || continue; echo "$p";
for a in name modes mode virtual_size bits_per_pixel stride blank; do
if [ -r "$p/$a" ]; then echo "$a"; cat "$p/$a"; fi; done; done''',
    'drm.txt': '''ls -l /sys/class/drm; for p in /sys/class/drm/*; do
[ -e "$p" ] || continue; echo "$p";
for a in status enabled modes connector_id; do
if [ -r "$p/$a" ]; then echo "$a"; cat "$p/$a"; fi; done; done''',
    'storage.txt': '''for p in /sys/class/scsi_device/*/device; do
[ -e "$p" ] || continue; echo "$p";
for a in vendor model rev type; do if [ -r "$p/$a" ]; then echo "$a"; cat "$p/$a"; fi; done; done;
for p in /sys/class/block/sd*/queue; do [ -e "$p" ] || continue; echo "$p";
for a in logical_block_size physical_block_size; do cat "$p/$a"; done; done''',
    'power-thermal.txt': '''for p in /sys/class/thermal/thermal_zone*; do
[ -e "$p" ] || continue; echo "$p"; cat "$p/type" "$p/temp"; done;
for p in /sys/class/power_supply/*; do [ -e "$p" ] || continue; echo "$p";
for a in type status capacity voltage_now current_now temp; do
if [ -r "$p/$a" ]; then echo "$a"; cat "$p/$a"; fi; done; done''',
    'cpu-topology.txt': '''cat /sys/devices/system/cpu/online /sys/devices/system/cpu/possible;
for p in /sys/devices/system/cpu/cpufreq/policy*; do
[ -e "$p" ] || continue; echo "$p";
for a in affected_cpus cpuinfo_max_freq cpuinfo_min_freq scaling_driver; do
if [ -r "$p/$a" ]; then echo "$a"; cat "$p/$a"; fi; done; done''',
    'debug-availability.txt': 'ls -l /sys/kernel/debug; ls -l /sys/kernel/debug/dri; ls -l /sys/fs/pstore',
    'module-files.txt': 'ls -l /vendor/lib/modules /vendor_dlkm/lib/modules /odm/lib/modules /odm_dlkm/lib/modules',
    'usb.txt': '''ls -l /sys/class/udc /sys/class/typec /sys/class/usb_role;
for p in /sys/class/udc/*; do [ -e "$p" ] || continue; echo "$p";
for a in state current_speed maximum_speed; do
if [ -r "$p/$a" ]; then echo "$a"; cat "$p/$a"; fi; done; done''',
}
BINARY = {
    'live.dtb': 'cat /sys/firmware/fdt',
    'live-tree.tar': 'tar -cf - -C /sys/firmware/devicetree/base .',
    'kernel-config.gz': 'cat /proc/config.gz',
}
PARTITIONS = [f'{base}_{slot}' for base in (
    'boot', 'init_boot', 'vendor_boot', 'dtbo', 'vbmeta', 'vbmeta_system',
    'vbmeta_vendor', 'lk', 'tee', 'gz', 'pvmfw') for slot in ('a', 'b')]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--serial')
    ap.add_argument('--partitions', action='store_true', help='Also read the fixed boot partition allowlist')
    args = ap.parse_args()
    if args.output.exists():
        raise SystemExit('Output already exists; use a new survey directory.')
    serial = args.serial
    if not serial:
        listing = subprocess.check_output(['adb', 'devices'], text=True)
        devices = [line.split()[0] for line in listing.splitlines() if '\tdevice' in line]
        if len(devices) != 1:
            raise SystemExit('Exactly one authorized ADB device required, or use --serial.')
        serial = devices[0]
    base = ['adb', '-s', serial, 'exec-out', 'su', '-c']

    def run(command, timeout=45):
        # exec-out transports arguments to su directly; shell-style extra quotes
        # would become part of the -c command on this ADB implementation.
        return subprocess.run(base + [command], capture_output=True, timeout=timeout)

    ident = run('id; getprop ro.product.device')
    if ident.returncode or b'uid=0' not in ident.stdout or b'degas' not in ident.stdout:
        raise SystemExit('Rooted degas identity check failed; nothing collected.')
    args.output.mkdir(parents=True)
    manifest = {'started_utc': dt.datetime.now(dt.timezone.utc).isoformat(),
                'serial': serial, 'mode': 'read-only hardware survey', 'captures': []}

    def save_manifest():
        (args.output/'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')

    def capture(name, command, timeout=45):
        record = {'file': name, 'command': command,
                  'started_utc': dt.datetime.now(dt.timezone.utc).isoformat()}
        try:
            result = run(command, timeout)
            (args.output/name).write_bytes(result.stdout)
            (args.output/(name+'.stderr.txt')).write_bytes(result.stderr)
            record.update(returncode=result.returncode, bytes=len(result.stdout),
                          sha256=hashlib.sha256(result.stdout).hexdigest())
        except subprocess.TimeoutExpired:
            record['error'] = 'timeout; capture not complete'
        manifest['captures'].append(record)
        save_manifest()
        print(f"{name}: {record.get('bytes', 0)} bytes, rc={record.get('returncode', record.get('error'))}", flush=True)
        return record

    for name, command in TEXT.items():
        capture(name, command)
    for name, command in BINARY.items():
        capture(name, command)
    if args.partitions:
        for part in PARTITIONS:
            path = '/dev/block/by-name/'+part
            info = run(f'blockdev --getsize64 {path}; sha256sum {path}', timeout=120)
            (args.output/(part+'.metadata.txt')).write_bytes(info.stdout + info.stderr)
            lines = info.stdout.splitlines()
            if info.returncode or len(lines) != 2 or not lines[0].isdigit():
                manifest['captures'].append({'file': part+'.img', 'error': 'partition metadata unavailable'})
                save_manifest()
                continue
            size = int(lines[0])
            if not 0 < size <= 128*1024*1024:
                raise SystemExit(f'Unexpected partition size: {part} {size}')
            expected = lines[1].split()[0].decode('ascii')
            rec = capture(part+'.img', 'cat '+path, timeout=180)
            rec['device_sha256'] = expected
            rec['verified'] = rec.get('returncode') == 0 and rec.get('bytes') == size and rec.get('sha256') == expected
            save_manifest()
            if not rec['verified']:
                raise SystemExit(f'Transfer verification failed for {part}; inspect evidence.')
    manifest['finished_utc'] = dt.datetime.now(dt.timezone.utc).isoformat()
    save_manifest()


if __name__ == '__main__':
    main()
