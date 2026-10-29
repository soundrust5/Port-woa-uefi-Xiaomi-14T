# Degas UFS, DMA, copy, and memory audit — 2026-10-26

Scope: offline, independent source and existing-evidence review. No phone, ADB,
fastboot, flashing, reboot, production-source edit, or new hardware inference.
The coordinator subsequently authorized updating only the stale BlockIO host
test, which was repaired and rerun as described below.
No AGENTS.md was found in the workspace or D:\ root. Read the complete relevant
bring-up history in `docs/DEGAS-DISPLAY-HANDOFF-20261024.md` and inspected the
current Checkpoint transport, copy, BlockIO, memory-map and inherited ARM MMU.

## Assessment

No definite read-data corruption, PRDT overflow, allocator/DMA overlap, or
incorrect cache-maintenance omission was found in the current path. V8 supports
a real CPU/application-path bottleneck: its transport test passed CRC at about
193 MiB/s, while its large synchronous read advanced about 5.69 MiB/s. V9's
64-bit copy is a plausible correction and is boundary-correct in host tests and
scalar in linked ARM64 code. V9's actual performance, full-file integrity, and
EBS remain unmeasured because its recovered log was stale.

The strongest remaining storage/memory candidates are incomplete integrity
coverage of the large read, memory headroom after loading a 618 MiB WIM into
roughly 1.06 GiB conventional RAM, and potentially insufficient copy performance
for the watchdog budget. They are candidates, not established explanations for
the V9 screen or absence of automatic reset.

## Confirmed source issue: successful read completion is not logged

`UfsBlockIo.h:53-62`: every successful crossing of `NextReport` immediately sets
`NextReport=TotalBytes+0x1000000`. Therefore the successful post-loop condition
`TotalBytes>=NextReport` is always false in normal operation; only errors reach
the `total_bytes/request_bytes/completed_blocks` record. The final successful
request can finish between progress intervals without any completion marker.

Confidence: high; an observability defect, not a data-path defect. It prevents
using absence of a final request record as proof that WIM I/O never completed.
An eventual diagnostic change should log one completion for sufficiently large
requests, including request bytes, completed bytes, and duration, while keeping
small metadata reads rate-limited. No change made in this audit.

## Descriptor and transfer bounds

Current definitions in `UfsCapacity.h:19-34`, `UfsReadCommand.h:1-21`:

| Object | Physical span / interpretation |
| --- | --- |
| Reserved arena | `[0x44470000,0x44480000)` — 64 KiB |
| UTRD slot 0 | `[0x44470000,0x44470020)` |
| Command descriptor base | `0x44471000`, 128-byte aligned |
| Request UPIU | `[0x44471000,0x44471020)` |
| Response area | base `0x44471080`; length 16 DWORDs = 64 bytes |
| One PRDT entry | `[0x44471100,0x44471110)` |
| Maximum payload | `[0x44472000,0x44480000)` — 14 x 4096 bytes |

UTRD DWORD6 `0x00200010` encodes response offset 32 DWORDs and response capacity
16 DWORDs. DWORD7 `0x00400001` encodes PRDT offset 64 DWORDs and length one entry.
PRDT byte count is `Bytes-1`; maximum 57343. Upper addresses are zero and the
bounce region is below 4 GiB, independently of where Windows allocates its
destination. The heap ends exactly at `0x44470000`, so it cannot allocate the
DMA arena (`MemoryMapLib.c:13-14`). No static region overlap was identified.

READ(10) validation checks LUN2, opcode, read direction, expected transfer
length, block count, maximum chunk and end LBA using subtraction to avoid
overflow. `UfsBioRead` validates the full caller request before narrowing LBA
to UINT32 (`UfsBlockIo.h:32-44`); capacity is below UINT32. Its chunk destination
offset is `I*4096`, matching completed blocks. Error paths do not copy rejected
payload and deactivate subsequent reads (`:45-51`). Tag wrap is acceptable for
one synchronous command at a time. The descriptor response and OCS are cleared
before reuse; payload is deliberately retained until a complete, accepted read.

The packet policy permits READ CAPACITY, bounded READ(10), and transient device
initialization queries. Block write callbacks return EFI_WRITE_PROTECTED and
the media declares ReadOnly (`UfsBlockIo.h:21-23,71-75`). This is suitable for a
read-only boot experiment. It is not proof every Windows feature can operate
without optional EFI filesystem/variable writes.

## Cache and ordering

The DMA memory-map entry uses `UNCACHED_UNBUFFERED`, which aliases
`ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED`. The inherited implementation
maps it to **Normal non-cacheable**, not Device:

- `mu/Mu_Basecore/UefiCpuPkg/Library/ArmMmuLib/AArch64/ArmMmuLibCore.c:83-90`
  selects `TT_ATTR_INDX_MEMORY_NON_CACHEABLE`; `:820-829` installs the MAIR value.
- `mu/Mu_Basecore/ArmPkg/Drivers/CpuDxe/AArch64/Mmu.c:75-77` exports that existing
  attribute to GCD as EFI_MEMORY_WC. CPU initialization synchronizes the existing
  page tables with GCD (`CpuDxe.c:472-474`); this is not a conversion to WB.
- `MemoryFence.S:30-34` is `dmb sy`. There is a fence before the doorbell and
  after observed completion (`UfsCapacity.h:27-34`). Thus the usual missing
  invalidate accusation is unsupported for this NC bounce buffer.

The caller receives CPU writes into its ordinary WB allocation; the controller
never DMAs directly into that caller allocation. No caller-cache invalidate is
needed for the copy itself. Do not change the DMA arena to WB or add direct DMA
to arbitrary caller pages without a real DMA/cache ownership design. A future
read-only inspection of actual translation attributes at `0x44470000` and the
destination can verify runtime state if a cache hypothesis remains.

## Copy correctness and generated code

`UfsCopy.h:3-13` reads aligned sources in eight-byte words, then a bounded tail.
Aligned callers receive aligned UINT64 stores; unaligned callers use CopyMem
from a local eight-byte word. An unaligned source uses the byte path. No read
crosses the requested interval. `IoAlign=1` is therefore supported.

The current linked image was independently disassembled:

`mu/Build/degasPkg/DEBUG_CLANGPDB/AARCH64/degasPkg/Drivers/CheckpointDxe/CheckpointDxe/DEBUG/DegasCheckpointDxe.dll`

SHA256 `49cdfb67f313f101895057acca70610b3c0ef1b62f995f37621b74ca6ea603fe`.
The map locates UfsCopyRead at RVA `0x8d90`. At `0x8db8`, it uses scalar
`ldr x8,[x20]`; aligned output uses `str x8,[x0]` at `0x8dd4`. There are no SIMD
or unaligned DMA loads in the function. LTO specialized the constant DMA source.
The compiler still spills the word to the stack each iteration for the fallback
branch, so eight-byte access does not by itself guarantee an eightfold overall
speedup. The unaligned destination path remains more expensive.

## Ranked next discriminating tests, when hardware is separately authorized

1. **Capture V10 automatic-reset evidence before changing transport.** Its
   purpose is to distinguish continuing reads, a return/error, an exception,
   and an observed EBS callback. Require a fresh log and the explicit EBS marker;
   no-reset or STARTING WINDOWS text cannot establish EBS. The diagnostic resets
   even if Windows progresses. V10 is a recovery/observability change, not a
   storage or memory fix.
2. **Read-plus-copy speed and full-file integrity.** Existing V9/V10
   `UfsSpeedCheck.h:13-32` already reports transport and copy time and CRC-checks
   copied data. Require its fresh values. If WIM read still dominates, add one
   read-only EFI filesystem test of the actual `sources/boot.wim` through the
   same BlockIO path, streaming through bounded allocated buffers, and compare
   SHA256 and exact byte count to the established staged ISO reference. Perform
   two different chunk sizes or a second pass if needed. This separates wrong
   file/LBA/copy/reuse from Windows loader behavior. The current repeated
   first-64-KiB CRC covers neither the WIM sectors nor a long destination range.
   High confidence in this coverage gap; no evidence currently demonstrates
   corruption.
3. **Memory map around the large allocation and before EBS.** Record largest
   conventional extent, total free bytes, and actual large-read destination
   range. The map has only the low heap and `[0x140000000,0x180000000)` as general
   RAM. Historical conventional total is 1,133,924,352 bytes. A 647,695,987-byte
   WIM alone leaves roughly 486 MB before other loader/runtime allocations.
   Additional expansion and contiguous-allocation needs are unknown, so neither
   adequate headroom nor out-of-memory is proven. The V8 `ConvertPages` failures
   precede continued successful reads and are insufficient to label fatal.
4. **If data mismatches implicate high RAM, validate the actual allocation.**
   Hardware densely tested only `[0x170000000,0x180000000)` (256 MiB), two full
   patterns plus cache maintenance, then freed it. The rest of the 1 GiB window
   had sparse 4 KiB samples at approximately 2 MiB strides. This excludes a claim
   of exhaustive validation of a 618 MiB destination. Use only pages obtained
   through AllocatePages, retain region guards and free them; do not test raw
   ConventionalMemory or enlarge the advertised map speculatively.

The read-plus-copy test must also be interpreted within the watchdog budget:
WBM feed cutoff is 60 seconds and hardware timeout about ten additional seconds.
For a roughly 618 MiB file, application-path throughput below roughly 10.3 MiB/s
cannot fit even the file transfer into the feed window; console, loader, and
other reads consume part of that window. Raising the timeout would only hide
this distinction. V8 clearly did not fit; V9 has no usable speed measurement.

## Offline tests performed

Ran current checked-in tests with WSL GCC -O2, output executables under `/tmp`:

- `diagnostic/tests/test_degas_ufs_copy.c`: PASS across source/destination
  offsets 0..15, lengths including 0/1/7/8/9/15/16/4096/57344/65536 and canaries.
- `test_degas_ufs_read_command.c`: PASS, command encoding, maximum payload,
  final LBA, oversize/zero/wrong opcode/LUN/length rejection.
- `test_degas_ufs_dma_reset.c`: PASS, exact UTRD/UPIU/PRDT resets, descriptor
  addresses/byte count, untouched payload and memory outside descriptor areas.
- Derived `/tmp/degas_dma_audit_blockio.c` from the existing BlockIO test, added
  current timing/copy/builder dependencies and a variable-size packet mock.
  PASS through real current `UfsBioRead` for unaligned 8 KiB output, 28 blocks in
  two chunks, 29 blocks ending at the final LBA in three chunks, byte content,
  canaries, watchdog-per-chunk, media/bounds errors, response tag/residual/
  transport failure and deactivated later reads. This is a host memory/logic
  test; it emulates neither ARM cache attributes nor UFS hardware.

The original checked-in `test_degas_ufs_blockio.c` was stale: compilation failed for
missing GetPerformanceCounter/DEGAS_UFS_MAX_BLOCKS/DegasUfsReadCommand/UfsCopyRead/
BootWatchdogFeed, and its packet mock insists on 4 KiB. This is a confirmed test
maintenance gap in the old test, not evidence that the firmware fails.

On explicit coordinator authorization, updated that test only: it now includes
the current builder/copy helper, accepts and validates variable-sized commands,
checks 14+14+1 batching and exact LBA progression, byte contents and canaries
for caller alignments 0..15, a three-chunk request ending at final LBA, invalid
inputs, zero length, read-only rejection, legacy file-slice bounds, and five
first/second-chunk faults (wrong tag, residual/underflow, transport rejection,
SCSI status, wrong response type). Fault tests verify partial output, untouched
unread bytes, deactivation, watchdog feed count and TPL restoration.

Reproduction from workspace root (WSL shell):

```sh
gcc -std=c11 -O2 -D_GNU_SOURCE -fshort-wchar -ffunction-sections -fdata-sections \
  -I mu/Mu_Basecore/MdePkg/Include -I mu/Mu_Basecore/MdePkg/Include/X64 \
  diagnostic/tests/test_degas_ufs_blockio.c -Wl,--gc-sections \
  -o /tmp/degas-ufs-blockio-current-audit
/tmp/degas-ufs-blockio-current-audit
```

Exit code 0, exact output:
`PASS: BlockIO 14+14+1 batching, all caller alignments/canaries, last LBA, read-only and first/second-chunk failures`

No production file was changed by this audit. The two repository writes are
this report and the explicitly authorized BlockIO test update.

## Existing hardware evidence used

- V8 speed: `evidence/raw/2026-10-25-degas-ufs-speed-v8-result/console-ramoops-0`,
  SHA256 `583e17211e8dd19a2dfa9319a68ebc062aab023cf183adab22f614c94fc58edb`.
- V8 WBM: `evidence/raw/2026-10-25-degas-windows-v8-result/console-ramoops-0`,
  SHA256 `c454db835334d197db209b1f6ef9af38e1f5a62b3fdbd75eea27dab7a8e13073`;
  lines 503-532 show regular successful progress to 403,247,104 cumulative bytes
  with 255,680,512 bytes remaining at request elapsed 65,666,816 us.
- RAM allocator: `evidence/raw/2026-10-25-degas-ram-allocator-v1-result/console-ramoops-0:92-98`,
  conventional total, allocated base, two 256 MiB passes and successful FreePages.
- V9 result directory has empty pstore and a copy of the V8 log. No V9 speed,
  integrity, read completion, EBS, or kernel-entry claim is supported.
