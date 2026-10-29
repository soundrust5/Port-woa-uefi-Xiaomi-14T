#!/usr/bin/env python3
"""Decode and extract an MTK AEE .dbg made of encrypted ZIP local records."""

import argparse, binascii, hashlib, json, struct, sys, zipfile, zlib
from collections import Counter
from pathlib import Path

HEADER = struct.Struct("<4s5H3I2H")


def masks(data):
    counts = Counter(data[i:i + 16] for i in range(0, len(data) - 15, 16))
    return [(b, n, data.find(b)) for b, n in counts.most_common() if n >= 2
            and bytes(data[i] ^ b[i] for i in range(4)) == b"PK\x03\x04"]


class ZipCrypto:
    def __init__(self, password):
        self.password = password
        self.k = [0x12345678, 0x23456789, 0x34567890]
        for c in password: self.update(c)
    def update(self, c):
        self.k[0] = binascii.crc32(bytes((c,)), self.k[0]) & 0xffffffff
        self.k[1] = ((self.k[1] + (self.k[0] & 255)) * 134775813 + 1) & 0xffffffff
        self.k[2] = binascii.crc32(bytes((self.k[1] >> 24,)), self.k[2]) & 0xffffffff
    def decrypt(self, data):
        # CPython's implementation is used to avoid CRC update convention
        # ambiguity in hand-written ZipCrypto implementations.
        return zipfile._ZipDecrypter(self.password)(data)


def parse(data, password, root=None):
    offset, found = 0, []
    while offset < len(data):
        if offset + HEADER.size > len(data): raise ValueError(f"trailing bytes at 0x{offset:x}")
        magic, ver, flags, method, tm, dt, crc, packed_n, size, name_n, extra_n = HEADER.unpack_from(data, offset)
        if magic != b"PK\x03\x04": raise ValueError(f"no local header at 0x{offset:x}: {magic.hex()}")
        start = offset + HEADER.size
        name = data[start:start + name_n]
        payload_start = start + name_n + extra_n
        payload = data[payload_start:payload_start + packed_n]
        if len(payload) != packed_n: raise ValueError(f"member {len(found)} overruns input")
        if flags & 1:
            clear = ZipCrypto(password).decrypt(payload)
            if len(clear) < 12 or clear[11] != crc >> 24:
                raise ValueError(f"member {len(found)} password check failed")
            packed = clear[12:]
        else: packed = payload
        content = packed if method == 0 else zlib.decompress(packed, -15) if method == 8 else None
        if content is None: raise ValueError(f"unsupported method {method}")
        actual_crc = binascii.crc32(content) & 0xffffffff
        if len(content) != size or actual_crc != crc:
            raise ValueError(f"member {len(found)} integrity mismatch")
        output_name = f"{len(found):03d}.bin"
        if root:
            root.mkdir(parents=True, exist_ok=True)
            (root / output_name).write_bytes(content)
        found.append({"index": len(found), "offset": offset,
                      "encoded_name_ascii": name.decode("ascii", "backslashreplace"),
                      "output_name": output_name, "flags": flags, "method": method,
                      "compressed_size": packed_n, "size": size, "crc32": f"{crc:08x}",
                      "sha256": hashlib.sha256(content).hexdigest()})
        offset = payload_start + packed_n
    return found


def metadata(data):
    offset, found = 0, []
    while offset < len(data):
        if offset + HEADER.size > len(data): raise ValueError(f"trailing bytes at 0x{offset:x}")
        magic, ver, flags, method, tm, dt, crc, packed_n, size, name_n, extra_n = HEADER.unpack_from(data, offset)
        if magic != b"PK\x03\x04": raise ValueError(f"no local header at 0x{offset:x}: {magic.hex()}")
        start = offset + HEADER.size
        payload_start = start + name_n + extra_n
        end = payload_start + packed_n
        if end > len(data): raise ValueError(f"member {len(found)} overruns input")
        found.append({"index": len(found), "offset": offset,
                      "encoded_name_ascii": data[start:start + name_n].decode("ascii", "backslashreplace"),
                      "flags": flags, "method": method, "compressed_size": packed_n,
                      "size": size, "crc32": f"{crc:08x}"})
        offset = end
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("source", type=Path); ap.add_argument("output", type=Path)
    ap.add_argument("--password", required=True)
    ap.add_argument("--metadata-only", action="store_true")
    a = ap.parse_args(); raw = a.source.resolve().read_bytes(); password = a.password.encode()
    valid, failures = [], []
    for mask, count, first in masks(raw):
        decoded = bytes(c ^ mask[i % 16] if i < 256 else c for i, c in enumerate(raw))
        try: valid.append((mask, count, first, decoded,
                           metadata(decoded) if a.metadata_only else parse(decoded, password)))
        except (ValueError, zlib.error) as e: failures.append({"mask": mask.hex(), "reason": str(e)})
    if len(valid) != 1:
        print(f"decode failed: {len(valid)} valid transforms; {failures}", file=sys.stderr); return 1
    mask, count, first, decoded, _ = valid[0]; a.output.mkdir(parents=True, exist_ok=True)
    (a.output / "decoded-local-records.zip").write_bytes(decoded)
    members = metadata(decoded) if a.metadata_only else parse(decoded, password, a.output / "members")
    report = {"source": str(a.source.resolve()), "source_size": len(raw),
      "source_sha256": hashlib.sha256(raw).hexdigest(),
      "outer_transform": "first 256 bytes: ciphertext XOR mask[file offset modulo 16]", "mask_hex": mask.hex(),
      "mask_derivation": {"candidate_source": "repeated aligned ciphertext blocks", "repetitions": count,
                          "first_offset": first, "opening_plaintext_hex": decoded[:4].hex()},
      "container": "ZIP local records without central directory",
      "password_validation": "not attempted (metadata only)" if a.metadata_only else
                             "ZipCrypto check byte, uncompressed size, and CRC-32 for every member",
      "validation": {"structurally_valid_candidates": 1, "member_count": len(members),
                     "payload_integrity_validated": not a.metadata_only}, "members": members}
    (a.output / "decode-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report["validation"], indent=2)); return 0


if __name__ == "__main__": raise SystemExit(main())
