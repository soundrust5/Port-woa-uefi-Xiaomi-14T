# Windows UFS performance candidate, 2026-10-27

Prepared while the phone is completing its first full Windows setup. No phone
access, reboot, live driver replacement, or partition writes for this work.

The current driver limits transfers to56KiB, uses a byte loop to copy every
payload, and has one outstanding command. Commands exceeding the50us short
poll window wait for a Storport timer callback. FUA requests deliberately wait
for a real SYNCHRONIZE CACHE; this durability guarantee is preserved.

New opt-in DEGAS_UFS_PERFORMANCE=1 candidate:

- Maximum transfer256KiB (64logical4KiB blocks), one PRDT descriptor.
- Private contiguous DMA allocation grows from68KiB to268KiB, covering the
  descriptor area, payload, and1024-byte alignment padding. The address and
  contiguous-length checks now cover the entire payload below4GiB.
- RtlCopyMemory replaces the byte-at-a-time copy routine.
- Storport physical-break limit supports the larger mapped request.
- Same one-slot ownership, completion checks, timeout quarantine, flush/FUA,
  and partition-scoped write checks. No IRQ or PHY/power changes.

UFSHCI single PRDT maximum256KiB is corroborated by Linux's
include/ufs/ufshci.h PRDT_DATA_BYTE_COUNT_MAX and drivers/ufs/core/ufshcd.c.
The larger request reduces the number of commands required for sequential
I/O by up to4.57times; it is not a measured throughput multiplier. Small-I/O
latency, timer scheduling and queue depth remain limitations.

Build: `wsl -d Ubuntu -- bash tools/build_degas_ufs.sh --performance`.
Package: `python tools/package_degas_ufs_install.py --performance`.
Output: artifacts/degas/ufs-windows/performance-candidate/test-package,
driver version0.2.0.0, local test signature, not Microsoft production signing.
Baseline install-candidate artifacts are preserved.

Validation: actual extracted request functions tested in all four combinations
of baseline/performance and scratch/install modes, GCC and Clang with ASan and
UBSan. Includes maximum-size READ10/16, full payload comparison, guarded
buffers, maximum writes ending exactly at authorized boundary, rejection of
cross-boundary writes, and FUA completion only after cache-sync success.
Results and source hashes: artifacts/degas/ufs-windows/host-tests/report.json.
Windows ARM64 compilation and INF verification passed; test package signed.

Signed SYS SHA256:
d1d41db322bbc47bb7554f5d8a589d4708e21f5082c697e0d89f18d73d9b2d88.

Hardware throughput and reliability have not been measured for this candidate.
Do not replace the running boot disk's driver during first-boot preparation.
Next hardware test should use the saved current driver as rollback and compare
read/write throughput and errors on files within the Windows NTFS partition.
