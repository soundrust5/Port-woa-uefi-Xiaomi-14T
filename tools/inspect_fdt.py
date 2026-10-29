"""Bounded FDT inventory; never assumes a bus address is a physical address.

Only direct root addresses and explicitly identity-mapped simple-bus/
reserved-memory descendants are labelled identity_to_root. Other translations
and vendor binary properties require binding/source review.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import struct


def parse(data):
    if len(data) < 40:
        raise ValueError('Truncated FDT header')
    magic, total, off_struct, off_strings, off_reserve, version, last, cpu, size_strings, size_struct = struct.unpack_from('>10I', data)
    if magic != 0xd00dfeed or not 17 <= version or last > 17 or not 40 <= total <= len(data):
        raise ValueError('Unsupported FDT header')
    if off_struct+size_struct > total or off_strings+size_strings > total:
        raise ValueError('FDT block outside totalsize')
    strings = data[off_strings:off_strings+size_strings]
    pos, end = off_struct, off_struct+size_struct
    stack, nodes = [], {}

    def cstring(buf, start, limit):
        stop = buf.find(b'\0', start, limit)
        if stop < 0:
            raise ValueError('Unterminated string')
        return buf[start:stop].decode('ascii'), stop+1

    terminated = False
    while pos+4 <= end:
        token = struct.unpack_from('>I', data, pos)[0]
        pos += 4
        if token == 1:
            name, pos = cstring(data, pos, end)
            pos = (pos+3) & ~3
            path = ((stack[-1].rstrip('/')+'/' if stack else '/')+name)
            if path in nodes:
                raise ValueError('Duplicate node')
            nodes[path] = {}
            stack.append(path)
        elif token == 2:
            if not stack:
                raise ValueError('Unbalanced node end')
            stack.pop()
        elif token == 3:
            if not stack or pos+8 > end:
                raise ValueError('Invalid property')
            length, nameoff = struct.unpack_from('>II', data, pos)
            pos += 8
            if pos+length > end or nameoff >= len(strings):
                raise ValueError('Property outside FDT blocks')
            name, _ = cstring(strings, nameoff, len(strings))
            if name in nodes[stack[-1]]:
                raise ValueError('Duplicate property')
            nodes[stack[-1]][name] = data[pos:pos+length]
            pos = (pos+length+3) & ~3
        elif token == 4:
            pass
        elif token == 9:
            if stack:
                raise ValueError('Unclosed nodes')
            terminated = True
            break
        else:
            raise ValueError(f'Unknown FDT token {token}')
    if not terminated:
        raise ValueError('Missing FDT_END')
    reserves = []
    pos = off_reserve
    while pos+16 <= total:
        address, size = struct.unpack_from('>QQ', data, pos)
        pos += 16
        if address == 0 and size == 0:
            break
        reserves.append({'address': hex(address), 'size': hex(size)})
    else:
        raise ValueError('Unterminated memory reservation block')
    return nodes, reserves, {'totalsize': total, 'version': version, 'boot_cpuid_phys': cpu}


def strings(raw):
    if raw and raw.endswith(b'\0') and all(b == 0 or 32 <= b < 127 for b in raw):
        return raw.rstrip(b'\0').decode('ascii').split('\0')
    return None


def cell(raw, default):
    if raw is None:
        return default
    if len(raw) != 4:
        raise ValueError('Invalid cells property')
    return int.from_bytes(raw, 'big')


def inventory(nodes):
    rows, warnings = [], []
    for path, props in nodes.items():
        raw = props.get('reg')
        if raw is None or path == '/':
            continue
        parent = path.rsplit('/', 1)[0] or '/'
        par = nodes[parent]
        ac = cell(par.get('#address-cells'), 2)
        sc = cell(par.get('#size-cells'), 1)
        width = 4*(ac+sc)
        if not 0 < ac <= 4 or not 0 <= sc <= 4 or len(raw) % width:
            warnings.append({'path': path, 'warning': 'reg length/cell format unresolved', 'raw_hex': raw.hex()})
            continue
        bus = parent
        identity = True
        while bus != '/':
            bp = nodes[bus]
            compat = strings(bp.get('compatible', b'')) or []
            if bp.get('ranges') != b'' or not ('simple-bus' in compat or bus == '/reserved-memory'):
                identity = False
                break
            bus = bus.rsplit('/',1)[0] or '/'
        for index, offset in enumerate(range(0, len(raw), width)):
            address = int.from_bytes(raw[offset:offset+4*ac], 'big')
            size = int.from_bytes(raw[offset+4*ac:offset+width], 'big') if sc else None
            rows.append({'path': path, 'reg_index': index, 'bus_address': hex(address),
                         'size': hex(size) if size is not None else '',
                         'address_kind': 'identity_to_root' if identity and sc else 'bus_or_identifier_unresolved',
                         'status_local': ','.join(strings(props.get('status', b'')) or ['absent']),
                         'compatible': ','.join(strings(props.get('compatible', b'')) or []),
                         'no_map': 'no-map' in props, 'reusable': 'reusable' in props})
    return rows, warnings


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('input', type=Path)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    data = args.input.read_bytes()
    nodes, reserves, header = parse(data)
    rows, warnings = inventory(nodes)
    encoded = {p: {k: {'hex': v.hex(), 'strings': strings(v)} for k,v in props.items()} for p,props in nodes.items()}
    result = {'source': str(args.input.resolve()), 'sha256': hashlib.sha256(data).hexdigest(),
              'header': header, 'memreserve': reserves, 'nodes': encoded,
              'reg_inventory': rows, 'warnings': warnings,
              'caveat': 'DT declarations, not register reads or permission to write. Local status does not include ancestor status. Bus translations beyond explicit simple-bus identity are unresolved.'}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    with args.output.with_suffix('.tsv').open('w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]) if rows else ['path'], delimiter='\t')
        writer.writeheader()
        writer.writerows(rows)
    print(f'{len(nodes)} nodes, {len(rows)} reg entries, {len(warnings)} unresolved reg properties')


if __name__ == '__main__':
    main()
