"""Cross-check captured artifacts on the host; never accesses the phone."""
from pathlib import Path
import hashlib
import json
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/vendor/python'))
from elftools.elf.elffile import ELFFile


def main():
    raw = ROOT / 'evidence/raw/2026-09-14-memory-display'
    extracted = ROOT / 'evidence/extracted/2026-09-14'
    checks = []

    def check(name, condition):
        checks.append({'name': name, 'passed': bool(condition)})

    for row in json.loads((raw / 'manifest.json').read_text()):
        data = (raw / row['file']).read_bytes()
        check('capture hash and size: ' + row['file'],
              len(data) == row['bytes'] and hashlib.sha256(data).hexdigest() == row['sha256'])
    dt = json.loads((extracted / 'live-fdt.json').read_text())
    check('fresh FDT equals inventoried FDT', hashlib.sha256((raw / 'live.dtb').read_bytes()).hexdigest() == dt['sha256'])
    nodes = dt['nodes']
    chosen = nodes['/chosen']
    blob = bytes.fromhex(chosen['atag,videolfb']['hex'])
    base, found, fps, size = struct.unpack_from('<QIII', blob)
    aux = lambda name: int(chosen['atag,videolfb-' + name]['hex'], 16)
    check('tag equals auxiliary fields', base == aux('fb_base_l') + (aux('fb_base_h') << 32) and size == aux('vramSize'))
    reg = bytes.fromhex(nodes['/reserved-memory/mblock-15-framebuffer']['reg']['hex'])
    check('tag equals reserved-memory reg', (base, size) == struct.unpack('>QQ', reg))
    dram_base, dram_size = struct.unpack('>QQ', bytes.fromhex(nodes['/memory']['reg']['hex']))
    check('framebuffer reserve inside declared DRAM', dram_base <= base < base + size <= dram_base + dram_size)
    log = (raw / 'pl_lk.txt').read_text(errors='replace')
    check('LK allocation matches DT', f'start: 0x{base:x}, sz: 0x{size:x}' in log)
    check('LK display init matches DT', f'fb_base:0x{base:x}' in log and f'size:{size}' in log)
    for elf_name, captured in [('mediatek-drm.ko', 'mediatek-drm-build-id.bin'),
                               ('panel-n12a-36-02-0b-dsc-cmd.ko', 'panel-build-id.bin')]:
        with (extracted / 'display-modules' / elf_name).open('rb') as f:
            note = ELFFile(f).get_section_by_name('.note.gnu.build-id').data()
        check('loaded build ID: ' + elf_name, note == (raw / captured).read_bytes())
    memory = json.loads((extracted / 'memory-reconciliation.json').read_text())
    check('fixed carveouts do not overlap', not memory['fixed_summary']['overlap_pairs'])
    check('mblock free intervals exactly complement fixed carveouts',
          memory['mblock_info']['exactly_equals_fixed_reservation_complement'])
    check('fixed and dynamic candidates do not overlap',
          not memory['fixed_plus_unique_dynamic_summary']['overlap_pairs'])
    check('named CMA sizes equal captured CmaTotal',
          sum(int(area['size'], 16) for area in memory['linux']['cma_areas'])
          == memory['linux']['meminfo_CmaTotal_kB'] * 1024)
    lk_dir = extracted / 'lk-analysis'
    lk_manifest = json.loads((lk_dir / 'container-manifest.json').read_text())
    stock = (lk_dir / lk_manifest['source']['local_copy']).read_bytes()
    captured_lk = (ROOT / lk_manifest['captured_partition_prefix']['path']).read_bytes()
    check('stock LK identity', hashlib.sha256(stock).hexdigest() == lk_manifest['source']['sha256'])
    check('stock LK equals captured active partition prefix', captured_lk[:len(stock)] == stock)
    for row in lk_manifest['payloads']:
        if 'payload_sha256' not in row:
            continue
        offset, length = int(row['payload_offset'], 16), int(row['payload_size'], 16)
        payload = stock[offset:offset + length]
        check('LK payload identity: ' + row['name'],
              len(payload) == length and hashlib.sha256(payload).hexdigest() == row['payload_sha256'])
    report = {'checks': checks, 'passed': all(x['passed'] for x in checks),
              'framebuffer': {'physical_reservation_base': hex(base), 'reservation_size': hex(size),
                              'end_exclusive': hex(base + size), 'tag_fps_raw': fps,
                              'panel_found': found},
              'scope': 'Artifact consistency only; does not prove payload entry, live LK scanout or permission to write memory.'}
    output = ROOT / 'evidence/validation-memory-display-2026-09-14.json'
    output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
    if not report['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
