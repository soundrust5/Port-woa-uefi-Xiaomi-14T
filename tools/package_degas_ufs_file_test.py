#!/usr/bin/env python3
"""Place a verified FAT16 test volume in stock boot_b padding; never flash.

The Android header/kernel stay byte-identical. FAT occupies boot_b +16..48MiB,
inside the existing 64MiB partition. Preserve stock AVB metadata/footer and all
other bytes outside the replacement boot payload and FAT interval. Retaining
metadata does NOT make the modified boot payload pass stock signature checks;
this remains an unlocked-device diagnostic. No GPT changes.
Requires dosfstools/mtools (Ubuntu WSL on Windows, native tools on Linux).
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess


def run(tool, *args):
    cmd = (["wsl", "-d", "Ubuntu", "--", tool] if os.name == "nt" else [tool])
    for arg in args:
        if isinstance(arg, Path):
            arg = arg.resolve().as_posix()
            if os.name == "nt":
                arg = "/mnt/" + arg[0].lower() + arg[2:]
        cmd.append(str(arg))
    subprocess.run(cmd, check=True)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def compose_partition(boot, volume, stock):
    offset, volume_bytes = 16 * 1024**2, 32 * 1024**2
    assert len(stock) == 64 * 1024**2 and len(volume) == volume_bytes
    magic, major, minor, original_size, vb_offset, vb_size, _ = struct.unpack(
        ">4sIIQQQ28s", stock[-64:])
    assert magic == b"AVBf" and (major, minor) == (1, 0)
    assert 0 < len(boot) <= original_size <= vb_offset
    assert vb_size >= 256 and vb_offset + vb_size <= offset
    assert stock[vb_offset:vb_offset+4] == b"AVB0"
    # Only this known-empty padding interval is available for the test volume.
    assert not any(stock[offset:offset+volume_bytes])
    packed = bytearray(stock)
    packed[:len(boot)] = boot
    packed[offset:offset+volume_bytes] = volume
    assert packed[len(boot):offset] == stock[len(boot):offset]
    assert packed[offset+volume_bytes:] == stock[offset+volume_bytes:]
    assert packed[vb_offset:vb_offset+vb_size] == stock[vb_offset:vb_offset+vb_size]
    return bytes(packed), dict(vbmeta_offset=vb_offset, vbmeta_bytes=vb_size,
        vbmeta_sha256=sha(stock[vb_offset:vb_offset+vb_size]),
        footer_sha256=sha(stock[-64:]), stock_signature_valid_for_modified_payload=False)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--boot", type=Path, required=True)
    p.add_argument("--efi", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--stock", type=Path, default=Path(__file__).resolve().parents[1] /
                   "evidence/raw/2026-10-24-degas-return/boot_b.img")
    a = p.parse_args()
    boot, efi = a.boot.read_bytes(), a.efi.read_bytes()
    stock = a.stock.read_bytes()
    assert sha(stock) == "fa01819342dd24164dea568f88f9715bc3132f738a68c49453907982946c1113"
    offset, volume_bytes = 16 * 1024**2, 32 * 1024**2
    assert boot[:8] == b"ANDROID!" and len(boot) < offset
    assert struct.unpack_from("<I", boot, 40)[0] == 4
    kernel, ramdisk = struct.unpack_from("<II", boot, 8)
    signature = struct.unpack_from("<I", boot, 1580)[0]
    align = lambda n: (n + 4095) & ~4095
    assert 4096 + align(kernel) + align(ramdisk) + align(signature) <= len(boot)
    assert efi[:2] == b"MZ"
    pe = struct.unpack_from("<I", efi, 60)[0]
    assert efi[pe:pe+4] == b"PE\0\0"
    assert struct.unpack_from("<H", efi, pe+4)[0] == 0xAA64
    assert struct.unpack_from("<H", efi, pe+24)[0] == 0x20B
    assert struct.unpack_from("<H", efi, pe+24+68)[0] == 10
    assert a.output.resolve() != a.boot.resolve() and not a.output.exists()
    fat = a.output.parent / "ufs-test-fat16.img"
    extracted = a.output.parent / "PROBE-readback.efi"
    assert not fat.exists() and not extracted.exists()
    with fat.open("xb") as f:
        f.truncate(volume_bytes)
    run("mkfs.fat", "--invariant", "-F", "16", "-S", "4096", "-s", "1", "-n", "DEGAS_TEST", fat)
    run("mcopy", "-i", fat, a.efi, "::/PROBE.EFI")
    run("fsck.fat", "-n", fat)
    run("mcopy", "-i", fat, "::/PROBE.EFI", extracted)
    assert extracted.read_bytes() == efi
    volume = fat.read_bytes()
    assert len(volume) == volume_bytes and volume[510:512] == b"\x55\xaa"
    assert struct.unpack_from("<H", volume, 11)[0] == 4096
    packed, avb = compose_partition(boot, volume, stock)
    assert len(packed) == len(stock)
    assert packed[:len(boot)] == boot and packed[offset:offset+volume_bytes] == volume
    with a.output.open("xb") as f:
        f.write(packed)
    assert a.output.read_bytes() == packed
    report = dict(boot_sha256=sha(packed), boot_bytes=len(packed),
                  original_boot_sha256=sha(boot), efi_sha256=sha(efi),
                  fat_sha256=sha(volume), fat_offset=offset, fat_bytes=volume_bytes,
                  target="boot_b only", boot_b_start_lba=444416,
                  slice_start_lba=448512, slice_blocks=8192, block_size=4096,
                  stock_template_sha256=sha(stock), preserved_avb=avb,
                  checks=["ARM64 EFI application", "fsck.fat -n", "mcopy byte-for-byte readback",
                          "Android boot payload unchanged", "entire image inside boot_b",
                          "stock AVB metadata/footer preserved", "all bytes outside two overlays preserved"])
    (a.output.parent / "fat-overlay-manifest.json").write_text(json.dumps(report, indent=2)+"\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
