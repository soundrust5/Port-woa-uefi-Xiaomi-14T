#!/usr/bin/env python3
"""Package and verify the first diagnostic kernel as an Android boot v4 image.

This is deliberately a host-only tool.  It never invokes adb or fastboot and it
does not produce a flashing script.  The stock boot_b header page is retained
byte-for-byte except for kernel_size, which must describe the replacement
payload.
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


PAGE_SIZE = 4096
HEADER_V4_SIZE = 1584
PARTITION_SIZE = 67_108_864

STOCK_BOOT_SHA256 = "fa01819342dd24164dea568f88f9715bc3132f738a68c49453907982946c1113"
MKBOOTIMG_SHA256 = "871ac22b7a1a9cae94a83860f8015b02f511afe15797909788fb6f9304f1d0ab"
UNPACK_BOOTIMG_SHA256 = "a9d260978a63bd06a24b6347e7dee8a28ff96639793caea15dff6aa491316308"
UNPACK_BOOTIMG_COMMIT = "d2bb0af5ba6d3198a3e99529c97eda1be0b5a093"
UNPACK_BOOTIMG_REPO = "https://android.googlesource.com/platform/system/tools/mkbootimg"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def relative(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return str(path.resolve())


def align(value: int, alignment: int = PAGE_SIZE) -> int:
    return (value + alignment - 1) // alignment * alignment


def parse_v4_header(page: bytes) -> dict[str, object]:
    if len(page) != PAGE_SIZE:
        raise ValueError(f"header page is {len(page)} bytes, expected {PAGE_SIZE}")
    if page[:8] != b"ANDROID!":
        raise ValueError("source does not begin with Android boot magic")
    kernel_size, ramdisk_size, os_version, header_size = struct.unpack_from("<4I", page, 8)
    reserved = list(struct.unpack_from("<4I", page, 24))
    header_version = struct.unpack_from("<I", page, 40)[0]
    cmdline_raw = page[44:1580]
    signature_size = struct.unpack_from("<I", page, 1580)[0]
    if header_version != 4 or header_size != HEADER_V4_SIZE:
        raise ValueError(
            f"source header is v{header_version} size {header_size}, expected v4 size {HEADER_V4_SIZE}"
        )
    if b"\0" in cmdline_raw:
        cmdline_bytes, tail = cmdline_raw.split(b"\0", 1)
        if any(tail):
            raise ValueError("source command-line field has non-zero bytes after its terminator")
    else:
        cmdline_bytes = cmdline_raw
    try:
        cmdline = cmdline_bytes.decode("ascii")
    except UnicodeDecodeError as error:
        raise ValueError("source command line is not ASCII") from error
    return {
        "magic": "ANDROID!",
        "kernel_size": kernel_size,
        "ramdisk_size": ramdisk_size,
        "os_version_patch_level_raw": os_version,
        "header_size": header_size,
        "reserved": reserved,
        "header_version": header_version,
        "cmdline": cmdline,
        "signature_size": signature_size,
    }


def require_hash(path: Path, expected: str, description: str) -> str:
    if not path.is_file():
        raise FileNotFoundError(f"missing {description}: {path}")
    actual = sha256(path)
    if actual != expected:
        raise ValueError(
            f"{description} hash mismatch for {path}: got {actual}, expected {expected}"
        )
    return actual


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=True, text=True, capture_output=True)


def verify_restore_provenance(repo: Path, stock_sha: str, stock_bytes: int) -> None:
    capture_path = repo / "evidence/raw/2026-09-14-initial/manifest.json"
    comparison_path = repo / "evidence/extracted/2026-09-14/stock-live-comparison.json"
    firmware_path = repo / "evidence/firmware-manifest.json"
    capture = json.loads(capture_path.read_text(encoding="utf-8"))
    capture_rows = [row for row in capture["captures"] if row.get("file") == "boot_b.img"]
    if len(capture_rows) != 1:
        raise ValueError("capture manifest does not contain exactly one boot_b.img record")
    row = capture_rows[0]
    expected_capture = {
        "command": "cat /dev/block/by-name/boot_b",
        "bytes": stock_bytes,
        "sha256": stock_sha,
        "device_sha256": stock_sha,
        "verified": True,
    }
    if any(row.get(key) != value for key, value in expected_capture.items()):
        raise ValueError("captured boot_b provenance does not match the selected restore image")

    comparison = json.loads(comparison_path.read_text(encoding="utf-8"))
    comparison_rows = [row for row in comparison if row.get("partition") == "boot_b"]
    if len(comparison_rows) != 1:
        raise ValueError("stock/live comparison does not contain exactly one boot_b record")
    comparison_row = comparison_rows[0]
    if comparison_row.get("stock_file") != "boot.img" or comparison_row.get("full_file_equal") is not True:
        raise ValueError("captured live boot_b is not recorded as fully equal to firmware boot.img")

    firmware = json.loads(firmware_path.read_text(encoding="utf-8"))
    firmware_rows = [row for row in firmware["files"] if row.get("name") == "boot.img"]
    if len(firmware_rows) != 1:
        raise ValueError("firmware manifest does not contain exactly one boot.img record")
    firmware_row = firmware_rows[0]
    if firmware_row.get("bytes") != stock_bytes or firmware_row.get("sha256") != stock_sha:
        raise ValueError("firmware boot.img identity differs from the captured live boot_b")


def package(args: argparse.Namespace) -> dict[str, object]:
    repo = Path(__file__).resolve().parents[1]
    payload = args.payload.resolve()
    stock = args.stock.resolve()
    output = args.output.resolve()
    manifest_path = args.manifest.resolve()
    restore_reference_path = args.restore_reference.resolve()
    mkbootimg = (repo / "mu/Resources/Scripts/mkbootimg.py").resolve()
    unpack_bootimg = (repo / "tools/vendor/unpack_bootimg.py").resolve()

    if not payload.is_file():
        raise FileNotFoundError(f"missing kernel payload: {payload}")
    if payload.stat().st_size == 0:
        raise ValueError("kernel payload is empty")
    expanded_bytes = 0
    expanded_hash = hashlib.sha256()
    image_header = b""
    with gzip.open(payload, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            if not image_header:
                image_header = block[:64]
            expanded_bytes += len(block)
            if expanded_bytes > 0x100000:
                raise ValueError("diagnostic probe exceeds its 1 MiB link bound")
            expanded_hash.update(block)
    if expanded_bytes == 0:
        raise ValueError("gzip kernel payload expands to an empty file")
    if len(image_header) != 64 or image_header[56:60] != b"ARM\x64":
        raise ValueError("expected diagnostic ARM64 Image header")
    if struct.unpack_from("<Q", image_header, 16)[0] != expanded_bytes:
        raise ValueError("probe Image memory size must equal complete serialized payload")
    protected = {stock, payload, mkbootimg, unpack_bootimg}
    outputs = {output, manifest_path, restore_reference_path}
    if len(outputs) != 3 or outputs & protected:
        raise ValueError("output paths must be distinct and cannot overwrite inputs")

    source_sha = require_hash(stock, STOCK_BOOT_SHA256, "stock boot_b image")
    mkbootimg_sha = require_hash(mkbootimg, MKBOOTIMG_SHA256, "pinned repository mkbootimg.py")
    unpack_sha = require_hash(
        unpack_bootimg, UNPACK_BOOTIMG_SHA256, "pinned AOSP unpack_bootimg.py"
    )
    if stock.stat().st_size != PARTITION_SIZE:
        raise ValueError(
            f"stock boot_b size is {stock.stat().st_size}, expected partition size {PARTITION_SIZE}"
        )
    verify_restore_provenance(repo, source_sha, stock.stat().st_size)

    with stock.open("rb") as stream:
        stock_page = stream.read(PAGE_SIZE)
    stock_header = parse_v4_header(stock_page)
    if stock_header["ramdisk_size"] != 0:
        raise ValueError("stock boot_b ramdisk is not empty")
    if stock_header["signature_size"] != 0:
        raise ValueError("stock boot_b contains a boot signature; this packager does not reproduce it")

    payload_size = payload.stat().st_size
    expected_image_size = PAGE_SIZE + align(payload_size)
    if expected_image_size > PARTITION_SIZE:
        raise ValueError(
            f"candidate would be {expected_image_size} bytes and exceed boot partition size {PARTITION_SIZE}"
        )

    output.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    restore_reference_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="boot-probe-", dir=output.parent) as temp_name:
        temp = Path(temp_name)
        empty_ramdisk = temp / "empty-ramdisk"
        empty_ramdisk.touch()
        mkbootimg_output = temp / "mkbootimg-output.img"
        unpacked = temp / "unpacked"

        mkbootimg_command = [
            sys.executable,
            str(mkbootimg),
            "--header_version",
            "4",
            "--kernel",
            str(payload),
            "--ramdisk",
            str(empty_ramdisk),
            "--cmdline",
            str(stock_header["cmdline"]),
            "--output",
            str(mkbootimg_output),
        ]
        run(mkbootimg_command)
        generated = mkbootimg_output.read_bytes()
        generated_header = parse_v4_header(generated[:PAGE_SIZE])
        if generated_header["kernel_size"] != payload_size:
            raise ValueError("mkbootimg encoded the wrong kernel size")
        if generated_header["ramdisk_size"] != 0:
            raise ValueError("mkbootimg unexpectedly emitted a ramdisk")
        if generated_header["cmdline"] != stock_header["cmdline"]:
            raise ValueError("mkbootimg did not preserve the stock command line")

        # This repository's older AOSP mkbootimg always appends a 4096-byte
        # zero-filled legacy GKI signature for v4.  boot_b explicitly has
        # signature_size=0, so retain only the header/kernel portion.  Verify
        # the discarded bytes before doing so; never label arbitrary bytes as
        # a signature and never synthesize an AVB structure.
        body_end = expected_image_size
        legacy_signature_size = int(generated_header["signature_size"])
        if legacy_signature_size != PAGE_SIZE:
            raise ValueError(
                f"pinned mkbootimg legacy signature size changed to {legacy_signature_size}"
            )
        if len(generated) != body_end + PAGE_SIZE:
            raise ValueError("pinned mkbootimg output layout changed unexpectedly")
        if any(generated[body_end:]):
            raise ValueError("pinned mkbootimg appended non-zero signature bytes")

        candidate_header_page = bytearray(stock_page)
        struct.pack_into("<I", candidate_header_page, 8, payload_size)
        candidate = bytes(candidate_header_page) + generated[PAGE_SIZE:body_end]
        if len(candidate) != expected_image_size:
            raise AssertionError("candidate size calculation is inconsistent")

        temp_candidate = temp / "boot-probe.img"
        temp_candidate.write_bytes(candidate)
        unpack_result = run(
            [
                sys.executable,
                str(unpack_bootimg),
                "--boot_img",
                str(temp_candidate),
                "--out",
                str(unpacked),
                "--format",
                "info",
            ]
        )
        roundtrip_kernel = unpacked / "kernel"
        roundtrip_ramdisk = unpacked / "ramdisk"
        if sha256(roundtrip_kernel) != sha256(payload):
            raise ValueError("unpacked kernel does not match the input payload")
        if roundtrip_ramdisk.stat().st_size != 0:
            raise ValueError("unpacked candidate ramdisk is not empty")
        candidate_header = parse_v4_header(candidate[:PAGE_SIZE])
        expected_header = dict(stock_header)
        expected_header["kernel_size"] = payload_size
        if candidate_header != expected_header:
            raise ValueError("candidate header differs from stock beyond kernel_size")

        shutil.copyfile(temp_candidate, output)

    payload_sha = sha256(payload)
    output_sha = sha256(output)
    manifest: dict[str, object] = {
        "schema": 1,
        "artifact": {
            "path": relative(output, repo),
            "bytes": output.stat().st_size,
            "sha256": output_sha,
            "partition_capacity_bytes": PARTITION_SIZE,
            "fits_partition": output.stat().st_size <= PARTITION_SIZE,
            "avb_footer_or_boot_signature_added": False,
        },
        "kernel_payload": {
            "path": relative(payload, repo),
            "bytes": payload_size,
            "sha256": payload_sha,
            "format_check": "gzip stream expands to non-empty data",
            "expanded_bytes": expanded_bytes,
            "expanded_sha256": expanded_hash.hexdigest(),
            "arm64_image_size_matches_expanded_bytes": True,
        },
        "stock_header_source": {
            "path": relative(stock, repo),
            "image_bytes": stock.stat().st_size,
            "sha256": source_sha,
            "header": stock_header,
            "preservation": "first 4096 bytes copied exactly; only little-endian kernel_size at offset 8 changed",
        },
        "restore_source": {
            "reference_path": relative(restore_reference_path, repo),
            "image_path": relative(stock, repo),
            "image_bytes": stock.stat().st_size,
            "sha256": source_sha,
            "capture": {
                "partition": "boot_b",
                "method": "cat /dev/block/by-name/boot_b",
                "capture_manifest": "evidence/raw/2026-09-14-initial/manifest.json",
                "capture_verified_against_device_sha256": True,
            },
            "firmware_stock_comparison": {
                "comparison_manifest": "evidence/extracted/2026-09-14/stock-live-comparison.json",
                "stock_file": "boot.img",
                "full_file_equal": True,
                "note": "The restore source remains the captured live boot_b path; equality does not silently substitute the firmware file.",
            },
        },
        "tools": {
            "mkbootimg": {
                "path": relative(mkbootimg, repo),
                "sha256": mkbootimg_sha,
                "identity": "repository-pinned AOSP mkbootimg.py copy used by mu/build_uefi.py",
                "legacy_zero_signature_removed": True,
            },
            "unpack_bootimg": {
                "path": relative(unpack_bootimg, repo),
                "sha256": unpack_sha,
                "upstream_repo": UNPACK_BOOTIMG_REPO,
                "upstream_commit": UNPACK_BOOTIMG_COMMIT,
                "upstream_file": "unpack_bootimg.py",
            },
        },
        "verification": {
            "header_version": 4,
            "header_size": HEADER_V4_SIZE,
            "page_size": PAGE_SIZE,
            "kernel_roundtrip_sha256": payload_sha,
            "ramdisk_roundtrip_bytes": 0,
            "signature_size": 0,
            "unpacker_stdout": unpack_result.stdout.replace(str(temp_candidate), "<candidate>"),
        },
        "scope": {
            "host_only": True,
            "phone_writes_or_reboots": False,
            "flash_script_generated": False,
            "bootloader_acceptance": "unknown; requires an unlocked-device boot test outside this tool",
        },
    }
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    restore_reference = {
        "schema": 1,
        "purpose": "hash-verified restore source reference; the 64 MiB image is not duplicated",
        "partition": "boot_b",
        "captured_live_image": relative(stock, repo),
        "bytes": stock.stat().st_size,
        "sha256": source_sha,
        "capture_manifest": "evidence/raw/2026-09-14-initial/manifest.json",
        "capture_command": "cat /dev/block/by-name/boot_b",
        "device_hash_was_verified_at_capture": True,
        "firmware_stock_was_full_file_equal_at_comparison": True,
        "comparison_manifest": "evidence/extracted/2026-09-14/stock-live-comparison.json",
        "restore_image_is_the_captured_live_path": True,
    }
    restore_reference_path.write_text(
        json.dumps(restore_reference, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(f"wrote {output} ({output.stat().st_size} bytes, sha256 {output_sha})")
    print(f"wrote {manifest_path}")
    print(f"wrote {restore_reference_path}")
    return manifest


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--kernel",
        "--payload",
        dest="payload",
        type=Path,
        default=repo / "diagnostic/build/entry.bin.gz",
        help="gzip-compressed replacement kernel payload",
    )
    parser.add_argument(
        "--stock",
        type=Path,
        default=repo / "evidence/raw/2026-09-14-initial/boot_b.img",
        help="exact stock boot_b evidence image",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=repo / "artifacts/first-test/boot-probe.img",
        help="candidate boot image path",
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=repo / "artifacts/first-test/manifest.json",
        help="verification manifest path",
    )
    parser.add_argument(
        "--restore-reference",
        type=Path,
        default=repo / "artifacts/first-test/restore-boot_b.json",
        help="small hash/provenance reference to the captured live restore image",
    )
    args = parser.parse_args()
    try:
        package(args)
    except (FileNotFoundError, OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError):
            if error.stdout:
                print(error.stdout, file=sys.stderr)
            if error.stderr:
                print(error.stderr, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
