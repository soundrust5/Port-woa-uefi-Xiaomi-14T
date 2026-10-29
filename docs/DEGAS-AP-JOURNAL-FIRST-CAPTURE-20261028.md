# First actual AP retained capture

Image `degas-aplog-integration-v1`, SHA256
`2e4f4ad2bd7cb44c92b0ed0c67292ce108d598d06e34b93bb09fad9b516ccec8`.
User reported HAL_INITIALIZATION_FAILED for this attempt.

Evidence: `evidence/raw/2026-10-28-degas-aplog-integration-result/decoded.json`
and original `dmesg-ramoops-30`, captured from Android pstore after recovery.
UUID `789aca72-dcb0-4d3b-8000-000000000001`, source-manifest hash verified.
AP1 commit/CRC/metadata valid; AP2..7 slots uncommitted. Console independently
records all seven APs ready before Windows and the 30-second EBS watchdog.

AP1 observed:

- Status `0000000200000003`: probe stage 2, invalid translation descriptor.
- Saved TPIDR `FFFFB90093E81000`.
- LPB `FFFFF801095D95A0`; physical diagnostic read of LPB+88 returned PRCB
  `FFFFF8010A4C0980`.
- Restored PC `FFFFF80112291BA0`; TTBR1 `004000004052A800`.
- MPIDR `81000100`, LocalId `100`, EL1, SCTLR M/C/I clear.

Stage 2 attempts to read PRCB.Number using
`(saved_TPIDR & ~FFF) + 980 + 20`, then selects the upper DWORD.
The attempted VA is `FFFFB90093E819A0`. With captured TCR and TTBR1,
its root index is 0x72, root descriptor PA 0x4052AB90. The LPB probe uses a
different root entry, index 0xF0, PA 0x4052AF80.

E18/E20/E28 zeros are unexecuted observations, not proof of processor number
zero or NULL queues. This capture does not prove Windows sees the same table
contents: the diagnostic reads physical memory with MMU/cache off, whereas
Windows may have newer data in Normal-WB cache lines. No kernel patch follows
from these observations. Next bounded diagnostic captures the failing level,
descriptor PA and raw descriptor already held by the reader, without new reads.

The console capture did not contain a completed post-crash raw-journal dump;
the independent pstore record did survive and validate. Do not claim that the
console fallback was hardware-qualified by this attempt.
