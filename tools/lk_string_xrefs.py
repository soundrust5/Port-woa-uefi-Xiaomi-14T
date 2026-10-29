"""Conservative adjacent ADRP+ADD / ADR string references in the pinned raw LK."""
import argparse
import hashlib
import re
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent/'vendor/python'))
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('pattern')
args = parser.parse_args()
data = (Path(__file__).resolve().parents[1]/'evidence/extracted/2026-09-14/lk-analysis/lk-payload.bin').read_bytes()
if hashlib.sha256(data).hexdigest() != '6afd84aefda089681f0ec4a224c6710228f525ec312bfa2532977be9e4496580':
    raise SystemExit('Unexpected LK payload; offsets and base require fresh analysis')
base = 0xffff000050700000
targets = {base+m.start():m.group().decode() for m in re.finditer(rb'[ -~]{6,}', data) if re.search(args.pattern, m.group().decode(), re.I)}
md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
md.skipdata = True
previous = None
for address, size, mnemonic, operands in md.disasm_lite(data, base):
    target = None
    if mnemonic == 'adr':
        match = re.fullmatch(r'x\d+, #?(0x[0-9a-f]+)', operands)
        if match: target = int(match[1], 16)
    elif mnemonic == 'add' and previous and previous[1] == 'adrp':
        page = re.fullmatch(r'(x\d+), #?(0x[0-9a-f]+)', previous[2])
        add = re.fullmatch(r'(x\d+), (x\d+), #?(0x[0-9a-f]+)', operands)
        if page and add and page[1] == add[2] and address == previous[0]+4:
            target = int(page[2],16)+int(add[3],16)
    if target in targets:
        print(f'{address:#x} -> {target:#x}: {targets[target]}')
    previous = (address, mnemonic, operands)
