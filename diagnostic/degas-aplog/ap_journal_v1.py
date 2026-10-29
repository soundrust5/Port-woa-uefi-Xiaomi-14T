"""Offline reference codec for the proposed degas AP journal; no device I/O.

This module does not install or enable a firmware writer. Its schema is fixed to
one existing 4 KiB ramoops dmesg zone, not the actively written console ring.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import uuid
import zlib

ZONE_BASE = 0x480AE000
ZONE_BYTES = 0x1000
PERSISTENT_SIG = 0x43474244
KMSG_HEADER = b"====0.000000-D\n"
HEADER_OFFSET = 0x40
HEADER_BYTES = 0x80
HEADER_MAGIC = b"DGAPJ001"
SLOT_MAGIC = b"DGAPS001"
SLOT_BYTES = 0x100
SLOT_COUNT = 7
END_OFFSET = 0x800
PAYLOAD_BYTES = END_OFFSET - 12
VERSION = 1
SNAPSHOT_NAMES = tuple(f"mailbox_{x:03x}" for x in range(0xE00, 0xE40, 8))
TELEMETRY_NAMES = ("ready", "mpidr", "sctlr_el1", "current_el", "local_id",
                   "entry", "release_observed", "last_mismatched_id")
CONTEXT_NAMES = ("initial_tpidr_el1", "lpb_va", "physical_ap_id", "raw_local_id", "status")


class JournalError(ValueError):
    pass


def _crc(data: bytes | bytearray) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def _seal(buf: bytearray, base: int, crc_offset: int, magic: bytes) -> None:
    checksum = _crc(buf[base + 8:base + crc_offset])
    struct.pack_into("<II", buf, base + crc_offset, checksum,
                     checksum ^ 0xFFFFFFFF)
    # A firmware writer must issue DSB SY before and after one aligned 64-bit
    # commit store, with mappings/cache policy established before CPU_ON.
    buf[base:base + 8] = magic


def _check(buf: bytes, base: int, crc_offset: int, magic: bytes) -> None:
    if buf[base:base + 8] != magic:
        raise JournalError("missing commit magic")
    actual, complement = struct.unpack_from("<II", buf, base + crc_offset)
    if actual ^ complement != 0xFFFFFFFF:
        raise JournalError("CRC complement mismatch")
    if _crc(buf[base + 8:base + crc_offset]) != actual:
        raise JournalError("CRC mismatch")


def make_reference_zone(attempt: uuid.UUID, build_sha256: bytes,
                        mailbox_base: int, counter: int = 0, flags: int = 0) -> bytearray:
    """Build a host fixture with seven uncommitted slots, never physical RAM."""
    if len(build_sha256) != 32 or mailbox_base % 0x1000:
        raise ValueError("require SHA-256 and page-aligned mailbox base")
    buf = bytearray(ZONE_BYTES)
    buf[12:12 + len(KMSG_HEADER)] = KMSG_HEADER
    base = HEADER_OFFSET
    struct.pack_into("<II", buf, base + 8, VERSION, HEADER_BYTES)
    buf[base + 0x10:base + 0x20] = attempt.bytes
    buf[base + 0x20:base + 0x40] = build_sha256
    struct.pack_into("<QIIQIIQ", buf, base + 0x40, mailbox_base, SLOT_COUNT,
                     SLOT_BYTES, ZONE_BASE, PAYLOAD_BYTES, flags, counter)
    _seal(buf, base, 0x78, HEADER_MAGIC)
    for cpu in range(1, 8):
        offset = cpu * SLOT_BYTES
        struct.pack_into("<II", buf, offset + 8, VERSION, SLOT_BYTES)
        buf[offset + 0x10:offset + 0x20] = attempt.bytes
        # Expected MPIDR affinity excludes architectural U/MT/RES1 flag bits;
        # actual raw MPIDR is retained in the telemetry copied by the AP.
        struct.pack_into("<QQQQ", buf, offset + 0x20, cpu << 8, cpu << 8,
                         mailbox_base + cpu * 0x1000, 0)
    # Linux ring start==size is valid for a non-wrapped, prepublished payload.
    # A real BSP publishes this 12-byte header only after all initialization.
    struct.pack_into("<III", buf, 0, PERSISTENT_SIG, PAYLOAD_BYTES, PAYLOAD_BYTES)
    return buf


def commit_reference_slot(buf: bytearray, cpu: int, snapshot: list[int],
                          telemetry: list[int], context: list[int]) -> None:
    """Model one immutable AP write for tests; reject a second publication."""
    if len(buf) != ZONE_BYTES or cpu not in range(1, 8):
        raise ValueError("invalid zone/cpu")
    if len(snapshot) != 8 or len(telemetry) != 8 or len(context) != 5:
        raise ValueError("require eight snapshot, eight telemetry, five context words")
    offset = cpu * SLOT_BYTES
    if any(buf[offset:offset + 8]):
        raise ValueError("slot already committed or corrupt")
    struct.pack_into("<8Q", buf, offset + 0x40, *snapshot)
    struct.pack_into("<8Q", buf, offset + 0x80, *telemetry)
    struct.pack_into("<5Q", buf, offset + 0xC0, *context)
    _seal(buf, offset, 0xF0, SLOT_MAGIC)


def export_reference_pstore(buf: bytes | bytearray) -> bytes:
    """Model stock ram_core save_old plus ram.c removal of its kmsg header."""
    return bytes(buf[12 + len(KMSG_HEADER):END_OFFSET])


def _normalize(data: bytes) -> bytes:
    if len(data) == ZONE_BYTES:
        sig, start, size = struct.unpack_from("<III", data)
        if (sig, start, size) != (PERSISTENT_SIG, PAYLOAD_BYTES, PAYLOAD_BYTES):
            raise JournalError("unexpected Linux zone metadata")
        if data[12:12 + len(KMSG_HEADER)] != KMSG_HEADER:
            raise JournalError("missing uncompressed Linux kmsg header")
        return data
    expected = PAYLOAD_BYTES - len(KMSG_HEADER)
    if len(data) != expected:
        raise JournalError(f"require raw 4096-byte zone or {expected}-byte pstore export")
    buf = bytearray(ZONE_BYTES)
    buf[12 + len(KMSG_HEADER):END_OFFSET] = data
    return bytes(buf)


def decode(data: bytes, expected_attempt: uuid.UUID,
           expected_build: bytes | None = None) -> dict:
    buf = _normalize(data)
    base = HEADER_OFFSET
    _check(buf, base, 0x78, HEADER_MAGIC)
    if struct.unpack_from("<II", buf, base + 8) != (VERSION, HEADER_BYTES):
        raise JournalError("unsupported journal header")
    attempt = uuid.UUID(bytes=buf[base + 0x10:base + 0x20])
    if not attempt.int:
        raise JournalError("zero attempt UUID")
    if attempt != expected_attempt:
        raise JournalError("stale/wrong attempt UUID")
    build = buf[base + 0x20:base + 0x40]
    if expected_build is not None and build != expected_build:
        raise JournalError("wrong build SHA-256")
    mailbox, count, stride, physical, payload, flags, counter = struct.unpack_from(
        "<QIIQIIQ", buf, base + 0x40)
    if (count, stride, physical, payload) != (
            SLOT_COUNT, SLOT_BYTES, ZONE_BASE, PAYLOAD_BYTES) or flags not in (0, 1):
        raise JournalError("unexpected journal layout/flags")
    if mailbox % 0x1000 or any(buf[base + 0x68:base + 0x78]):
        raise JournalError("invalid mailbox alignment/reserved header")
    records = []
    for cpu in range(1, 8):
        offset = cpu * SLOT_BYTES
        result = {"cpu": cpu, "valid": False}
        if not any(buf[offset:offset + 8]):
            result["status"] = "not committed"
            records.append(result)
            continue
        try:
            _check(buf, offset, 0xF0, SLOT_MAGIC)
            if struct.unpack_from("<II", buf, offset + 8) != (VERSION, SLOT_BYTES):
                raise JournalError("unsupported AP slot")
            if buf[offset + 0x10:offset + 0x20] != attempt.bytes:
                raise JournalError("wrong AP attempt UUID")
            affinity, local_id, page, reserved = struct.unpack_from("<4Q", buf, offset + 0x20)
            snap = struct.unpack_from("<8Q", buf, offset + 0x40)
            telem = struct.unpack_from("<8Q", buf, offset + 0x80)
            context = struct.unpack_from("<5Q", buf, offset + 0xC0)
            if affinity != cpu << 8 or local_id != cpu << 8:
                raise JournalError("wrong AP affinity/local ID")
            if page != mailbox + cpu * 0x1000 or not telem[5]:
                raise JournalError("wrong AP mailbox/empty entry")
            if (affinity, local_id) != (telem[1] & 0xFF00FFFFFF, telem[4]):
                raise JournalError("AP metadata/telemetry mismatch")
            if reserved or any(buf[offset + 0xE8:offset + 0xF0]) or any(buf[offset + 0xF8:offset + 0x100]):
                raise JournalError("nonzero reserved AP bytes")
            result.update(valid=True, status="committed", mailbox=f"0x{page:016x}",
                          snapshot=dict(zip(SNAPSHOT_NAMES, (f"0x{x:016x}" for x in snap))),
                          telemetry=dict(zip(TELEMETRY_NAMES, (f"0x{x:016x}" for x in telem))),
                          context=dict(zip(CONTEXT_NAMES, (f"0x{x:016x}" for x in context))))
            # Explicit payload discriminator in the private context status.
            # This is a parked-CPU observer, not an OS release/pre-entry record.
            if context[4] >> 32 in (0x42434731,0x42434732):
                v2=context[4] >> 32 == 0x42434732
                result['record_kind'] = 'bugcheck_monitor_v2' if v2 else 'bugcheck_monitor_v1'
                result['context'] = (dict(nt_base=f'0x{context[0]:016x}',
                    last_problem_controller=f'0x{context[1]:016x}',
                    last_problem_file=f'0x{context[2]:016x}',last_problem_line=context[3],
                    kind_tag='BCG2',status=context[4] & 0xffffffff) if v2 else
                    dict(nt_base=f'0x{context[0]:016x}',source_lpb=f'0x{context[1]:016x}',
                    physical_ap_id=context[2],raw_local_id=context[3],kind_tag='BCG1',status=context[4] & 0xffffffff))
                result['bugcheck_monitor'] = dict(status=snap[0],
                    initialized=snap[1] & 0xffffffff, active=snap[1] >> 32,
                    code=f'0x{snap[2]:x}', parameters=[f'0x{x:016x}' for x in snap[3:7]],
                    counter=snap[7], observed_stable=snap[0] == 0,
                    scope='Repeated stable samples, not a transactional guarantee for concurrent bugchecks')
                if v2:
                    result['bugcheck_monitor'].update(last_problem=telem[7] >> 32,
                        last_problem_status=f'0x{telem[7]&0xffffffff:08x}',
                        last_problem_controller=f'0x{context[1]:016x}',
                        last_problem_file=f'0x{context[2]:016x}',last_problem_line=context[3])
                result['telemetry'] = dict(ready=telem[0],mpidr=f'0x{telem[1]:016x}',
                    sctlr_el1=f'0x{telem[2]:016x}',current_el=telem[3],local_id=telem[4],
                    monitor_entry=f'0x{telem[5]:016x}',os_release_observed=telem[6],
                    monitor_auxiliary=telem[7])
        except JournalError as error:
            result["status"] = str(error)
        records.append(result)
    identity_key = "source_manifest_sha256" if flags == 1 else "build_sha256"
    return {"schema": VERSION, "attempt": str(attempt), identity_key: build.hex(),
            "identity_kind": identity_key, "flags": flags,
            "counter": counter, "complete": all(x["valid"] for x in records),
            "records": records}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--attempt", required=True, type=uuid.UUID)
    parser.add_argument("--build-sha256", "--source-manifest-sha256", dest="identity_hash", type=bytes.fromhex)
    args = parser.parse_args()
    try:
        result = decode(args.capture.read_bytes(), args.attempt, args.identity_hash)
    except (JournalError, OSError) as error:
        parser.exit(2, f"AP journal rejected: {error}\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
