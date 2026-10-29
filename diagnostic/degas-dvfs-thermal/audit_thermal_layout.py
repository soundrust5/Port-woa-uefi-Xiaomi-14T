"""Host-only source/DT extraction. Does not access a device or physical memory."""
from pathlib import Path
import hashlib
import json
import re
import struct

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
PIN = '3c4ccd07d5624879555ec710c11bb8818e5cb5c6'
FDT = ROOT / 'evidence/raw/2026-10-25-degas-ufs-baseline-v2/live-fdt.json'
fdt = json.loads(FDT.read_text())
node = fdt['nodes']['/therm-intf@114000']
reg = bytes.fromhex(node['reg']['hex'])
base, size = struct.unpack('>QQ', reg)
names = node['reg-names']['strings']
assert names == ['therm_sram'] and (base, size) == (0x114000, 0x400)
header = (HERE / 'thermal_interface.h').read_text()
constants = {name: int(val, 0) for name, val in re.findall(
    r'^#define\s+(\w+)\s+\((0x[0-9A-Fa-f]+|[0-9]+)\)', header, re.M)}
fields = {}
for name, count in [('CPU_TEMP_OFFSET', 8), ('DSU_AVG_TEMP_BASE_ADDR_OFFSET', 1),
                    ('CPU_LIMIT_FREQ_OFFSET', 3), ('CPU_CUR_FREQ_OFFSET', 3),
                    ('CPU_MAX_TEMP_OFFSET', 3), ('CPU_LIMIT_OPP_OFFSET', 3),
                    ('DSU_CEILING_FREQ_OFFSET', 1), ('UTC_COUNT_OFFSET', 1)]:
    offset = constants[name]
    assert offset + 4 * count <= size
    fields[name] = {'offset': hex(offset), 'count': count,
                    'addresses': [hex(base + offset + 4*i) for i in range(count)]}
report = {'scope': 'source-and-saved-DT only; not active thermal-module qualification',
          'source_pin': PIN, 'dtb_sha256_reported': fdt['sha256'],
          'node': '/therm-intf@114000', 'reg_names': names,
          'base': hex(base), 'size': hex(size), 'cputcm_present': False,
          'fields': fields,
          'source_files': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in [HERE / 'thermal_interface.c', HERE / 'thermal_interface.h', HERE / 'Makefile']},
          'phone_accessed': False, 'mmio_accessed': False,
          'freshness_or_firmware_autonomy_proven': False}
(HERE / 'thermal-layout.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
