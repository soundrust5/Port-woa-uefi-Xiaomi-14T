#!/usr/bin/env python3
"""Decode and safely extract the XOR-obfuscated ZIP used by an MTK AEE .dbg.

The mask is not assumed.  Candidate 16-byte masks are taken from repeated,
16-byte-aligned ciphertext blocks.  A candidate is accepted only if it decodes
the input as a ZIP and every member CRC verifies.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
import tempfile
import zipfile
from collections import Counter
from pathlib import Path, PurePosixPath


def xor_bytes(data: bytes, mask: bytes) -> bytes:
    return bytes(value ^ mask[index % len(mask)] for index, value in enumerate(data))


def candidate_masks(data: bytes) -> list[tuple[bytes, int, int]]:
    blocks = Counter(data[index:index + 16] for index in range(0, len(data) - 15, 16))
    candidates = []
    for block, count in blocks.most_common():
        if count < 2:
            break
        first_offset = data.find(block)
        if bytes(data[index] ^ block[index] for index in range(4)) == b"PK\x03\x04":
            candidates.append((block, count, first_offset))
    return candidates


def safe_member_path(root: Path, member: str) -> Path:
    path = PurePosixPath(member)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError(f"unsafe ZIP member path: {member!r}")
    return root.joinpath(*path.parts)


def decode(source: Path, output: Path) -> dict:
    raw = source.read_bytes()
    accepted = []
    failures = []
    for mask, repetitions, first_offset in candidate_masks(raw):
        decoded = xor_bytes(raw, mask)
        try:
            with tempfile.SpooledTemporaryFile(max_size=32 * 1024 * 1024) as stream:
                stream.write(decoded)
                stream.seek(0)
                with zipfile.ZipFile(stream) as archive:
                    bad_member = archive.testzip()
                    if bad_member is not None:
                        failures.append({"mask": mask.hex(), "reason": f"CRC failed: {bad_member}"})
                        continue
                    accepted.append((mask, repetitions, first_offset, decoded, archive.infolist()))
        except (OSError, ValueError, zipfile.BadZipFile) as exc:
            failures.append({"mask": mask.hex(), "reason": str(exc)})

    if len(accepted) != 1:
        raise RuntimeError(f"expected exactly one CRC-valid transform, found {len(accepted)}; failures={failures}")

    mask, repetitions, first_offset, decoded, infos = accepted[0]
    output.mkdir(parents=True, exist_ok=True)
    decoded_zip = output / "decoded.zip"
    decoded_zip.write_bytes(decoded)
    members = []
    with zipfile.ZipFile(decoded_zip) as archive:
        for info in archive.infolist():
            destination = safe_member_path(output / "members", info.filename)
            if info.is_dir():
                destination.mkdir(parents=True, exist_ok=True)
            else:
                destination.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(info) as src, destination.open("wb") as dst:
                    shutil.copyfileobj(src, dst)
            members.append({
                "name": info.filename,
                "compressed_size": info.compress_size,
                "size": info.file_size,
                "crc32": f"{info.CRC:08x}",
            })

    report = {
        "source": str(source),
        "source_size": len(raw),
        "source_sha256": hashlib.sha256(raw).hexdigest(),
        "transform": "ciphertext byte XOR mask[file_offset modulo 16]",
        "mask_hex": mask.hex(),
        "mask_derivation": {
            "candidate_source": "repeated 16-byte-aligned ciphertext blocks",
            "repetitions": repetitions,
            "first_offset": first_offset,
            "opening_plaintext": decoded[:4].hex(),
            "opening_plaintext_ascii": "PK\\x03\\x04",
        },
        "validation": {
            "crc_valid_candidates": 1,
            "zip_testzip": "all member CRCs valid",
            "zip_member_count": len(infos),
        },
        "decoded_zip_sha256": hashlib.sha256(decoded).hexdigest(),
        "members": members,
    }
    (output / "decode-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        report = decode(args.source.resolve(), args.output.resolve())
    except Exception as exc:
        print(f"decode failed: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(report["validation"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
