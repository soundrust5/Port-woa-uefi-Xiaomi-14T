"""Match second-test watchdog state to the preserved readback payload."""
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/vendor/python'))
from elftools.elf.elffile import ELFFile

dump = (ROOT / 'evidence/raw/second-test-recovery/expdb-full.img').read_bytes()
assert len(dump) == 134217728
assert hashlib.sha256(dump).hexdigest() == '8e5c055ea2e29eb078be917a433eb896b2f5383433f0bd0ebbdf70ec7a53aa3d'
payload = (ROOT / 'diagnostic/build-readback/entry.bin').read_bytes()
validation = json.loads((ROOT / 'artifacts/second-test/validation.json').read_text())
assert hashlib.sha256(payload).hexdigest() == validation['payload_sha256']
with (ROOT / 'diagnostic/build-readback/entry.elf').open('rb') as stream:
    table = ELFFile(stream).get_section_by_name('.symtab')
    symbols = {name: table.get_symbol_by_name(name)[0]['st_value'] for name in
               ['diagnostic_readback_ok', 'diagnostic_readback_failed', '__stack_end', 'diagnostic_marker_readback']}
assert payload[0xfc:0x100] == bytes.fromhex('9f3f03d5')  # DSB after return
call = struct.unpack_from('<I', payload, 0xf8)[0]
assert call >> 26 == 0b100101
imm = call & 0x3ffffff
if imm & (1 << 25):
    imm -= 1 << 26
assert 0xf8 + 4 * imm == symbols['diagnostic_marker_readback']
matches = []
for m in re.finditer(rb'cpu\(0\) pc:<([0-9a-f]+)> lr:<([0-9a-f]+)> sp:<([0-9a-f]+)> pstate=([0-9a-f]+)', dump):
    pc, lr, sp, pstate = [int(x, 16) for x in m.groups()]
    base = sp - symbols['__stack_end']
    if lr != base + 0xfc:
        continue
    for name in ['diagnostic_readback_ok', 'diagnostic_readback_failed']:
        offset = symbols[name]
        if pc not in (base + offset, base + offset + 4):
            continue
        assert payload[offset:offset+8] == bytes.fromhex('5f2003d5ffffff17')
        excerpt = dump[max(0, m.start()-800):m.start()+1100].decode('ascii', errors='replace')
        out = ROOT / 'evidence/extracted/second-test-expdb'
        out.mkdir(exist_ok=True)
        (out / 'probe-watchdog.txt').write_text(excerpt)
        matches.append(dict(expdb_offset=hex(m.start()), pc=hex(pc), lr=hex(lr), sp=hex(sp),
                            pstate=hex(pstate), inferred_load_base=hex(base), status_symbol=name))
assert len(matches) == 1, matches
report = dict(payload_sha256=validation['payload_sha256'], boot_image_sha256=validation['boot_image_sha256'],
              expdb_sha256=hashlib.sha256(dump).hexdigest(), matches=matches,
              readback_passed=matches[0]['status_symbol'] == 'diagnostic_readback_ok',
              limits=['Attribution uses exact PC/LR/SP and recorded flash; no build ID recovered from RAM.',
                      'CPU-visible framebuffer readback does not prove physical scanout or panel refresh.',
                      'Older first-test watchdog also remains in expdb; file offset is not chronological order.'])
(ROOT / 'evidence/second-test-watchdog-analysis.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report, indent=2))
