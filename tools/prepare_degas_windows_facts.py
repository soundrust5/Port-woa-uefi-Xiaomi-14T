"""Offline DT evidence and RAM candidates, NOT an enabled firmware memory map.

Historical Linux allocations are exclusions, never proof of ownership in Mu.
No phone commands, firmware patching or ACPI installation are performed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

from reconcile_memory import merge, subtract, interval

ROOT = Path(__file__).resolve().parents[1]
DT = ROOT / 'evidence/raw/2026-10-25-degas-ufs-baseline-v2/live-fdt.json'
HISTORY = ROOT / 'evidence/extracted/2026-09-14/memory-reconciliation.json'


def raw(node, name):
    return bytes.fromhex(node[name]['hex'])


def number(node, name, width=None):
    data = raw(node, name)
    if len(data) not in (4, 8) or (width is not None and len(data) != width):
        raise ValueError(f'invalid integer property {name}')
    return int.from_bytes(data, 'big')


def strings(node, name):
    return raw(node, name).rstrip(b'\0').decode('ascii').split('\0')


def ranges(node, ac, sc):
    if ac not in (1, 2) or sc not in (1, 2):
        raise ValueError('unsupported address/size cell count')
    data = raw(node, 'reg')
    stride = 4 * (ac + sc)
    if not data or len(data) % stride:
        raise ValueError('truncated reg property')
    result = []
    for i in range(0, len(data), stride):
        start = int.from_bytes(data[i:i + 4 * ac], 'big')
        size = int.from_bytes(data[i + 4 * ac:i + stride], 'big')
        if not size or start + size > 1 << 64:
            raise ValueError('empty or overflowing reg interval')
        result.append((start, start + size))
    return result


def ppis(node, cells, count):
    data = raw(node, 'interrupts')
    if cells != 4 or len(data) != count * cells * 4:
        raise ValueError('unexpected GIC interrupt layout')
    out = []
    for i in range(count):
        kind, irq, flags, partition = struct.unpack_from('>4I', data, i * 16)
        if kind != 1 or irq >= 16 or flags not in (1, 2, 4, 8) or partition:
            raise ValueError('unsupported PPI encoding')
        out.append({'intid': irq + 16, 'dt_flags': flags,
                    'acpi_flags': {1: 1, 2: 3, 4: 0, 8: 2}[flags]})
    return out


def pairs(rows):
    return [(int(r['start'], 0), int(r['end_exclusive'], 0)) for r in rows]


def extract(dt, history):
    nodes = dt['nodes']
    root = nodes['/']
    if 'mediatek,MT6897' not in strings(root, 'compatible'):
        raise ValueError('not the captured MT6897 platform')
    ac, sc = number(root, '#address-cells'), number(root, '#size-cells')
    dram = merge([r for path, node in nodes.items()
                  if path in ('/memory',) or path.startswith('/memory@')
                  for r in ranges(node, ac, sc)])
    if dram != [(0x40000000, 0x340000000)]:
        raise ValueError('DRAM differs from captured 12 GiB device')

    reserved = nodes['/reserved-memory']
    rac, rsc = number(reserved, '#address-cells'), number(reserved, '#size-cells')
    if raw(reserved, 'ranges'):
        raise ValueError('reserved-memory address translation is unsupported')
    excluded = []
    unresolved = []

    def exclude(start, end, source):
        if start >= end:
            raise ValueError('invalid exclusion')
        excluded.append(interval(start, end, source=source))

    for path, node in nodes.items():
        if not path.startswith('/reserved-memory/'):
            continue
        if 'reg' in node:
            for start, end in ranges(node, rac, rsc):
                exclude(start, end, path)
        elif 'size' in node:
            unresolved.append({'path': path, 'size': hex(number(node, 'size')),
                               'reason': 'no fixed base in current FDT'})
    # inspect_fdt.py represents memreserve as address/size; reject unknown forms.
    for item in dt['memreserve']:
        start = int(item['address'], 0)
        exclude(start, start + int(item['size'], 0), 'FDT memreserve')

    # Existing firmware arena and LK-owned DTB stay out of ADDITIONAL RAM.
    exclude(0x40000000, 0x48080000, 'existing low arena plus LK DTB reservation')
    # Preserve all historical bootloader allocations conservatively.
    for row in history['lk']['largest_reserved_checkpoint']:
        exclude(int(row['start'], 0), int(row['end_exclusive'], 0),
                'historical LK: ' + row['name'])
    for row in history['linux']['live_memblock']['reserved_regions']:
        exclude(int(row['start'], 0), int(row['end_exclusive'], 0),
                'historical Linux memblock reserved')
    # Framebuffer is inside Linux System RAM in the capture; it must be removed.
    exclude(0xfcc2f000, 0xfd919000, 'active framebuffer')
    chosen = nodes['/chosen']
    exclude(number(chosen, 'linux,initrd-start'), number(chosen, 'linux,initrd-end'),
            'current FDT initrd')
    # Intersection with observed Linux RAM is only a candidate filter.
    candidates = merge([(max(a, c), min(b, d)) for a, b in dram
                        for c, d in pairs(history['linux']['top_level_System_RAM'])
                        if max(a, c) < min(b, d)])
    candidates = subtract(candidates, pairs(excluded))
    candidates = [( (a + 4095) & ~4095, b & ~4095) for a, b in candidates]
    candidates = [(a, b) for a, b in candidates if a < b]

    cpus = []
    cpu_cells = number(nodes['/cpus'], '#address-cells')
    if cpu_cells not in (1, 2):
        raise ValueError('unsupported CPU address cells')
    for path, node in nodes.items():
        if not path.startswith('/cpus/cpu@'):
            continue
        if 'status' in node and strings(node, 'status') != ['okay']:
            raise ValueError('unexpected disabled CPU')
        if strings(node, 'enable-method') != ['psci']:
            raise ValueError('CPU is not PSCI managed')
        cpus.append({'path': path, 'mpidr': hex(number(node, 'reg', cpu_cells * 4)),
                     'compatible': strings(node, 'compatible')})
    cpus.sort(key=lambda c: int(c['mpidr'], 0))
    if len(cpus) != 8 or len({c['mpidr'] for c in cpus}) != 8:
        raise ValueError('expected eight unique CPU affinities')
    gic = nodes['/interrupt-controller']
    if strings(gic, 'compatible') != ['arm,gic-v3']:
        raise ValueError('unsupported interrupt controller')
    gic_ranges = ranges(gic, ac, sc)
    if len(gic_ranges) != 2:
        raise ValueError('expected GICD and one GICR region')
    irq_cells = number(gic, '#interrupt-cells')
    timers = ppis(nodes['/timer'], irq_cells, 4)
    for item, role in zip(timers, ['secure physical', 'nonsecure physical', 'virtual', 'hypervisor']):
        item['role'] = role
    if strings(nodes['/psci'], 'method') != ['smc']:
        raise ValueError('PSCI conduit differs from SMC')
    return {
        'schema': 1, 'status': 'OFFLINE CANDIDATES ONLY; NOT INSTALLED',
        'dram': [interval(a, b) for a, b in dram],
        'additional_ram_candidates': [interval(a, b) for a, b in candidates],
        'candidate_bytes': sum(b - a for a, b in candidates),
        'exclusions': excluded, 'unresolved_dynamic_reservations': unresolved,
        'cpus': cpus, 'psci_conduit': 'smc',
        'gic': {'distributor': interval(*gic_ranges[0]),
                'redistributor_region': interval(*gic_ranges[1]),
                'maintenance': ppis(gic, irq_cells, 1)[0],
                'per_cpu_redistributor_bases': None},
        'timer': {'dt_frequency_hz': number(nodes['/timer'], 'clock-frequency'),
                  'always_on_declared': 'always-on' in nodes['/timer'], 'interrupts': timers},
        'blockers': [
            'Resolve dynamic carveouts and current LK ownership before publishing RAM.',
            'Historical Linux RAM is not proof of current EL1/stage-2 access in Mu.',
            'Read boot MPIDR and GICR_TYPER affinities; never infer per-CPU GICR bases from CPU index.',
            'Verify runtime CNTFRQ and PSCI behavior before enabling ACPI or secondary CPUs.',
            'Resolve watchdog and command-mode display refresh at ExitBootServices.',
            'Keep UFS/BlockIO alive through Windows image loading; current test deliberately cleans up.',
        ],
    }


def gtdt_draft(facts):
    """Build only GTDT; no MADT/XSDT/installation until runtime CPU evidence exists."""
    entries = facts['timer']['interrupts']
    words = [v for e in entries for v in
             (e['intid'], e['acpi_flags'] | (4 if facts['timer']['always_on_declared'] else 0))]
    body = struct.pack('<Q9IQ4I', 0xffffffffffffffff, 0, *words,
                       0xffffffffffffffff, 0, 0, 0, 0)
    result = bytearray(struct.pack('<4sIBB6s8sI4sI', b'GTDT', 36 + len(body),
                                  3, 0, b'XIAOMI', b'DEGAS   ', 1, b'DGAS', 1) + body)
    result[9] = -sum(result) & 255
    return bytes(result)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dt', type=Path, default=DT)
    ap.add_argument('--history', type=Path, default=HISTORY)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    facts = extract(json.loads(args.dt.read_text()), json.loads(args.history.read_text()))
    facts['sources'] = [{'path': str(p), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
                        for p in (args.dt, args.history)]
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'windows-platform-candidates.json').write_text(json.dumps(facts, indent=2) + '\n')
    (args.output / 'GTDT.draft.bin').write_bytes(gtdt_draft(facts))
    print(f"Candidate additional RAM: {facts['candidate_bytes'] / (1024 ** 2):.2f} MiB; "
          f"{len(facts['additional_ram_candidates'])} ranges. Not enabled.")
    print('GTDT draft only. MADT/DSDT and runtime installation intentionally absent.')


if __name__ == '__main__':
    main()
