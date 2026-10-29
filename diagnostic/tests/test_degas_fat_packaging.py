"""Regression for lost AVB metadata in the first FAT-overlay image."""
import importlib.util
from pathlib import Path
import struct

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("pack", root / "tools/package_degas_ufs_file_test.py")
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)
stock = (root / "evidence/raw/2026-10-24-degas-return/boot_b.img").read_bytes()
boot = (root / "artifacts/degas-ufs-efi-v1/boot-uefi.img").read_bytes()
fat = (root / "artifacts/degas-ufs-efi-v1/ufs-test-fat16.img").read_bytes()
bad = (root / "artifacts/degas-ufs-efi-v1/boot.img").read_bytes()
_, _, _, _, vb, n, _ = struct.unpack(">4sIIQQQ28s", stock[-64:])
assert bad[vb:vb+4] == bytes(4) and stock[vb:vb+4] == b"AVB0"
fixed, report = pack.compose_partition(boot, fat, stock)
assert len(fixed) == 67108864 and fixed[-64:] == stock[-64:]
assert fixed[vb:vb+n] == stock[vb:vb+n]
assert fixed[:len(boot)] == boot
assert fixed[16777216:50331648] == fat
damaged = bytearray(stock)
damaged[vb:vb+4] = bytes(4)
try:
    pack.compose_partition(boot, fat, damaged)
except AssertionError:
    pass
else:
    raise AssertionError("damaged AVB template accepted")
print("PASS: failed image loses AVB, corrected packaging preserves metadata and boundaries")
