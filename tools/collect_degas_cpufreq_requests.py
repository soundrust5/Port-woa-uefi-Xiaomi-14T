"""Observe the running driver's existing request read; never write DVFS state."""
from pathlib import Path
import argparse
import hashlib
import json
import shlex
import uuid

from degas_full_windows_stage_source import shell

ROOT = Path(__file__).resolve().parents[1]
NOTE = bytes.fromhex('040000001400000003000000474e5500068e6f5000761002c3541edbe177a210f2299352')
MODULE_SHA = 'f12addd9a3b5c677e8fec7b11903561e2d4073dff831a5ce4bd4dc7057df51fc'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--status', action='store_true', help='Also read the mapped readiness and latency words')
    args = parser.parse_args()
    module = ROOT / 'artifacts/degas/dvfs-active-modules/ramdisk00/mediatek-cpufreq-hw.ko'
    assert hashlib.sha256(module.read_bytes()).hexdigest() == MODULE_SHA
    assert shell('getprop ro.product.device').strip() == b'degas'
    assert shell('getprop sys.boot_completed').strip() == b'1'
    assert shell('cat /sys/module/mediatek_cpufreq_hw/notes/.note.gnu.build-id') == NOTE
    args.output.mkdir(parents=True, exist_ok=False)
    name = 'degas_dvfs_' + uuid.uuid4().hex[:12]
    instance = '/sys/kernel/tracing/instances/' + name
    # get+0x30 follows readl_relaxed. X0 is the existing MMIO result, X20
    # points to ordinary driver RAM. The pointer fetches below do not read SRAM.
    event = ('p:' + name + '/get mediatek_cpufreq_hw:mtk_cpufreq_hw_get+0x30 '
             'raw=%x0:x32 domain=%x20:x64 req_va=+24(%x20):x64 '
             'lut_va=+8(%x20):x64 ready_va=+32(%x20):x64 '
             'latency_va=+48(%x20):x64 nr_opp=+56(%x20):u32 cpus=+64(%x20):x64')
    if args.status:
        event += ' ready=+0(+32(%x20)):x32 latency=+0(+48(%x20)):x32 enable=+0(+16(%x20)):x32'
    # At get+0x24 X8 is the relocated domain-map base (.bss+8). The two
    # bytes are in module RAM, not the old/unmapped CSRAM global mapping.
    entry = ('p:' + name + '/entry mediatek_cpufreq_hw:mtk_cpufreq_hw_get+0x24 '
             'cpu=%x0:u32 domain=%x20:x64 map=%x8:x64 '
             'mode_flag=+272(%x8):u8 fdvfs_flag=+276(%x8):u8')
    script = f'''set -e
tr=/sys/kernel/tracing
instance={shlex.quote(instance)}
cleanup() {{
  if test -d "$instance"; then
    echo 0 > "$instance/tracing_on"
    if test -f "$instance/events/{name}/get/enable"; then echo 0 > "$instance/events/{name}/get/enable"; fi
    if test -f "$instance/events/{name}/entry/enable"; then echo 0 > "$instance/events/{name}/entry/enable"; fi
    rmdir "$instance"
  fi
  printf '%s\\n' '-:{name}/get' >> "$tr/kprobe_events"
  printf '%s\\n' '-:{name}/entry' >> "$tr/kprobe_events"
}}
trap cleanup EXIT
mkdir "$instance"
echo 0 > "$instance/tracing_on"
echo 256 > "$instance/buffer_size_kb"
printf '%s\\n' {shlex.quote(event)} >> "$tr/kprobe_events"
printf '%s\\n' {shlex.quote(entry)} >> "$tr/kprobe_events"
echo 1 > "$instance/events/{name}/get/enable"
echo 1 > "$instance/events/{name}/entry/enable"
echo 1 > "$instance/tracing_on"
for round in 1 2 3 4; do
  for policy in 0 4 7; do
    echo "ROUND $round POLICY $policy"
    cat /sys/devices/system/cpu/cpufreq/policy$policy/cpuinfo_cur_freq
    cat /sys/devices/system/cpu/cpufreq/policy$policy/scaling_cur_freq
    cat /sys/devices/system/cpu/cpufreq/policy$policy/scaling_available_frequencies
  done
done
echo 0 > "$instance/tracing_on"
echo DEGAS_TRACE_BEGIN
cat "$instance/trace"
echo DEGAS_TRACE_END
'''
    (args.output / 'capture-command.sh').write_text(script, encoding='utf-8')
    capture = shell(script)
    (args.output / 'capture.txt').write_bytes(capture)
    assert b'DEGAS_TRACE_BEGIN' in capture and b'DEGAS_TRACE_END' in capture
    trace = capture.split(b'DEGAS_TRACE_BEGIN\n', 1)[1].split(b'DEGAS_TRACE_END', 1)[0]
    assert b'get:' in trace, 'No probe hits: do not infer request values from sysfs alone'
    assert name.encode() not in shell('cat /sys/kernel/tracing/kprobe_events')
    (args.output / 'manifest.json').write_text(json.dumps(dict(
        phase='Android, ordinary cpuinfo_cur_freq reads', module_sha256=MODULE_SHA,
        module_build_id=NOTE[-20:].hex(), probes=[entry, event],
        added_sram_reads=['base+0x8c readiness', 'base+0x114 latency', 'base+0x84 enable'] if args.status else [],
        performance_control_writes=False,
        tracing_instance_removed=True, probe_removed=True), indent=2) + '\n')
    print('PASS: exact-driver request reads captured; no DVFS writes; temporary probe removed')


if __name__ == '__main__':
    main()
