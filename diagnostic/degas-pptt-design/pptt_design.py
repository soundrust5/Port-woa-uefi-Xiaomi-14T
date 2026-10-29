"""Independent topology-only PPTT fixture; never installs firmware tables.

One physical package, eight non-SMT leaves, no cache geometry assertions.
This is an independent design artifact for comparison with the firmware owner's
implementation. No MADT/PSCI/native-context/mailbox settings are edited.
"""
from pathlib import Path
import hashlib
import json
import struct

HERE = Path(__file__).resolve().parent
HEADER_SIZE, NODE_SIZE, ROOT_OFFSET = 36, 20, 36
CPU_COUNT = 8


def build_topology_only(revision=3):
    if revision not in (2, 3):
        raise ValueError('this design uses revision2+ leaf/thread flags')
    body = bytearray(struct.pack('<BBHIIII', 0, NODE_SIZE, 0, 1, 0, 0, 0))
    for uid in range(CPU_COUNT):
        body += struct.pack('<BBHIIII', 0, NODE_SIZE, 0, 0xA, ROOT_OFFSET, uid, 0)
    header = bytearray(struct.pack('<4sIBB6s8sI4sI', b'PPTT', HEADER_SIZE + len(body),
                                   revision, 0, b'XIAOMI', b'DEGAS   ', 1, b'DGAS', 1))
    data = header + body
    data[9] = -sum(data) & 255
    return bytes(data)


def validate_topology_only(data):
    if len(data) != 216 or data[:4] != b'PPTT' or struct.unpack_from('<I', data, 4)[0] != len(data):
        raise ValueError('exact signature/length')
    if data[8] not in (2, 3) or sum(data) & 255:
        raise ValueError('revision/checksum')
    nodes = {}
    for at in range(HEADER_SIZE, len(data), NODE_SIZE):
        kind, length, reserved, flags, parent, uid, resources = struct.unpack_from('<BBHIIII', data, at)
        if kind != 0 or length != NODE_SIZE or reserved or resources or flags & ~0x1F:
            raise ValueError('node fields')
        nodes[at] = dict(flags=flags, parent=parent, uid=uid)
    root = nodes[ROOT_OFFSET]
    if root != dict(flags=1, parent=0, uid=0):
        raise ValueError('one non-identical physical package without ACPI ID')
    seen = set()
    for at, node in nodes.items():
        if at == ROOT_OFFSET:
            continue
        if node['flags'] != 0xA or node['parent'] != ROOT_OFFSET or node['uid'] in seen:
            raise ValueError('unique direct non-SMT leaves')
        seen.add(node['uid'])
    if seen != set(range(CPU_COUNT)):
        raise ValueError('exact MADT UID coverage')
    return nodes


def ccsidr_maintenance_geometry(raw, id_aa64mmfr2):
    """Decode maintenance parameters, NOT physical ownership or guaranteed size."""
    ccidx = (id_aa64mmfr2 >> 20) & 15
    if ccidx == 0:
        ways, sets = ((raw >> 3) & 0x3FF) + 1, ((raw >> 13) & 0x7FFF) + 1
    elif ccidx == 1:
        ways, sets = ((raw >> 3) & 0x1FFFFF) + 1, ((raw >> 32) & 0xFFFFFF) + 1
    else:
        raise ValueError('reserved CCIDX encoding')
    line = 1 << ((raw & 7) + 4)
    return dict(ccidx=ccidx, line_bytes=line, ways=ways, sets=sets,
                maintenance_capacity_product=line * ways * sets,
                actual_cache_size_proven=False, sharing_proven=False)


def clidr_selectors(clidr):
    """Only selectors reported by CLIDR; no tag-cache or absent-bank access."""
    out = []
    for level in range(1, 8):
        kind = (clidr >> ((level - 1) * 3)) & 7
        if kind == 0:
            continue
        if kind not in (1, 2, 3, 4):
            raise ValueError('reserved Ctype')
        base = (level - 1) << 1
        if kind in (2, 3, 4):
            out.append((level, 'unified' if kind == 4 else 'data', base))
        if kind in (1, 3):
            out.append((level, 'instruction', base | 1))
    return out


if __name__ == '__main__':
    out = HERE / 'out'
    out.mkdir(exist_ok=True)
    data = build_topology_only()
    nodes = validate_topology_only(data)
    (out / 'PPTT.topology-only.bin').write_bytes(data)
    report = dict(scope='HOST ONLY, not installed', revision=data[8], length=len(data),
                  sha256=hashlib.sha256(data).hexdigest(), packages=1, cores=8,
                  cache_nodes=0, nodes={hex(at): node for at, node in nodes.items()})
    (out / 'design.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
