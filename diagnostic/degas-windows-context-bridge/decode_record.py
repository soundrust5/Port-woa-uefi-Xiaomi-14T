"""Decode CRC-qualified native-context record from a ramoops/pstore capture."""
from pathlib import Path
import argparse, json, struct, uuid, zlib

def decode(data, manifest=None, raw_slot=False):
    records = []
    cursor = 0
    while True:
        offset = data.find(b'NCTX0001', cursor)
        if offset < 0:
            break
        cursor = offset + 8
        record = data[offset:offset + 256]
        if len(record) != 256:
            continue
        u32 = lambda n: struct.unpack_from('<I', record, n)[0]
        u64 = lambda n: struct.unpack_from('<Q', record, n)[0]
        crc = zlib.crc32(record[8:240]) & 0xffffffff
        if u32(8) != 1 or u32(12) != 256 or u32(240) != crc or u32(244) != (crc ^ 0xffffffff):
            continue
        outer = offset - 0x40
        looks_raw = outer >= 0 and data[outer + 4:outer + 12] == struct.pack('<II', 0x1f4, 0x1f4)
        if raw_slot or looks_raw:
            if outer < 0 or data[outer:outer + 12] != struct.pack('<III', 0x43474244, 0x1f4, 0x1f4):
                continue
        result = dict(build_id=str(uuid.UUID(bytes=record[16:32])), source_manifest_sha256=record[32:64].hex(),
                      sequence=u64(64), stage=u32(72), mode=u32(76), mailbox_base=hex(u64(80)),
                      kernel_physical=hex(u64(88)), kernel_virtual=hex(u64(96)),
                      status=u32(104), pages_scanned=u32(108), mz_candidates=u32(112),
                      exact_matches=u32(116), write_attempted=u32(120), patched=u32(124),
                      advertised_cpus=u32(128), descriptors=u32(132), map_status=hex(u64(136)),
                      performance_counter=u64(144), record_offset=offset,
                      access_fault_page=hex(u64(152)), access_fault_par=hex(u64(160)),
                      outer_commit_checked=bool(raw_slot or looks_raw),
                      commit_scope='raw DBGC commit checked' if raw_slot or looks_raw else 'payload-only pstore; outer header unavailable',
                      scope='UEFI installation result only; not proof Windows accepted secondary CPUs')
        if manifest is not None:
            if result['build_id'] != manifest['build_id'] or result['source_manifest_sha256'] != manifest['source_manifest_sha256']:
                raise ValueError('retained record does not match the supplied build manifest')
        records.append(result)
    if not records:
        raise ValueError('no CRC-valid native-context record found')
    return records

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--raw-slot', action='store_true', help='Require raw-slot DBGC commit header before the record')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text()) if args.manifest else None
    records = decode(args.capture.read_bytes(), manifest, raw_slot=args.raw_slot)
    text = json.dumps(records, indent=2) + '\n'
    if args.output:
        args.output.write_text(text)
    print(text, end='')
