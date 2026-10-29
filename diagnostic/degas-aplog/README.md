# Degas retained AP journal v1

These files define the transport, decoder and host tests for eight AP diagnostic
words plus handoff context. The platform has a BSP initializer in
`mu/Platforms/Xiaomi/degasPkg/Include/ApJournal.h`, generic prior-journal recovery
in CheckpointDxe, and an AP commit call before the Windows branch. AP2..AP7 retain
their raw handoff context; the translated PRCB snapshot is currently AP1 only.

**Transport qualification passed on 2026-10-28.** The phone reported
`RAM JOURNAL RETAINED` after a watchdog reset into UEFI. The same header-only
canary survived END TEST watchdog -> fastboot -> restored stock boot_b -> Android,
which exported the exact 2021-byte `dmesg-ramoops-30`. UUID, hash and header CRC
passed decoding; all seven AP commits were zero as expected. Evidence is in
`evidence/raw/2026-10-28-degas-aplog-retention-result/`, including
`qualification.json`, the binary export and pstore tar. This proves that tested
transport path for a BSP canary. It does not prove AP execution, Windows crash
capture, hard-reset retention or power-loss retention.

The 2026-10-28 source audit found that the existing console ring is not an
available immutable journal: RamSerialPortLib uses every byte of its capacity,
protects writers only against IRQ preemption on the BSP, and Windows WINUFS has
its own later writer to the same ring. An AP must not append through that sink.

## Placement and ownership

Existing stock configuration is recorded in
`evidence/raw/ramoops-survey/live.txt` and
`evidence/raw/2026-09-14-initial/live.dts`; the matching upstream source snapshot
is in `evidence/sources/ramoops/`. This is a source/configuration match, not proof
that every instruction of the running vendor kernel matches upstream.

| Region | Half-open physical interval | Existing owner |
|---|---|---|
| Whole pstore reservation | 0x48090000..0x48170000 | Stock reserved-memory, no-map |
| Dmesg records 0..29 | 0x48090000..0x480ae000 | Android ramoops |
| **Journal: dmesg record 30** | **0x480ae000..0x480af000** | **Replaces one existing record** |
| Console | 0x480af000..0x480ef000 | BSP serial and Windows WINUFS |
| Ftrace reservation | 0x480ef000..0x480f0000 | Android ramoops even with frontend disabled |
| Pmsg | 0x480f0000..0x48170000 | Android ramoops |

Live values were record_size=0x1000, ECC=0, mem_type=0. Therefore no ECC trailer
occupies candidate record 30. Do not use this layout if those parameters change.
Record 30 is not unused RAM: capture and preserve its current contents, along
with all pstore files, before a test intentionally replaces it. Do not carve
space out of the console or use ftrace: ftrace has a different signature seed.

MemoryMapLib reserves this 4 KiB as uncached memory, excluded from DXE and
Windows allocations. The static map must not gain an overlapping conventional
region. Do not infer mapping from physical proximity to console.
No new MMIO region or raw UFS operation is necessary.

## Linux envelope

All integers are little endian. Zone offsets below are relative to 0x480ae000.

| Offset | Bytes | Value |
|---|---:|---|
| 0x000 | 4 | Linux persistent RAM signature 0x43474244, dmesg seed 0 |
| 0x004 | 4 | start=0x7f4 |
| 0x008 | 4 | size=0x7f4 |
| 0x00c | 15 | Exact ASCII `====0.000000-D\n` |
| 0x01b..0x03f | 37 | Zero padding |
| 0x040..0x0bf | 128 | Journal header |
| 0x0c0..0x0ff | 64 | Zero padding |
| 0x100..0x7ff | 7 x 256 | Fixed AP1..AP7 slots |
| 0x800..0xfff | 2048 | Zero, outside the published payload |

Linux accepts start==size for this non-wrapped payload. It reconstructs the
2036 bytes beginning at offset 0x00c and removes the 15-byte kmsg header before
exporting a 2021-byte uncompressed `dmesg-ramoops-30` file. A missing/invalid kmsg
header makes ram.c zap/skip the dmesg record. The console format alone is not
sufficient. The exact `-D` is essential: `-C` falsely requests decompression.

The decoder accepts exactly a 4096-byte raw zone or the 2021-byte pstore export.
Raw decoding checks the Linux signature, exact start/size and kmsg header.
Pstore exports no longer contain those envelope bytes; the stock kernel already
validated them. Both forms independently require the private namespace magic,
version, layout, UUID and CRC. No arbitrary substring searching is performed.

## Private header and per-AP slots

Header offsets are relative to zone+0x40:

| Offset | Bytes | Field |
|---|---:|---|
| 0x00 | 8 | Commit/namespace magic `DGAPJ001` |
| 0x08 | 4 | Version=1 |
| 0x0c | 4 | Header bytes=128 |
| 0x10 | 16 | Attempt UUID, canonical UUID bytes (`uuid.UUID.bytes`, not bytes_le) |
| 0x20 | 32 | Identity SHA-256, interpreted according to flags |
| 0x40 | 8 | Base of the eight 4 KiB mailbox pages |
| 0x48 | 4 | Slot count=7 |
| 0x4c | 4 | Slot bytes=256 |
| 0x50 | 8 | Physical journal base=0x480ae000 |
| 0x58 | 4 | Published Linux payload bytes=0x7f4 |
| 0x5c | 4 | Flags=0: legacy canary identity; flags=1: source-manifest SHA-256 |
| 0x60 | 8 | Flags=1: retained attempt sequence; flags=0: informational canary field |
| 0x68 | 16 | Reserved zero |
| 0x78 | 4 | CRC32 over header[0x08:0x78] |
| 0x7c | 4 | Bitwise complement of CRC32 |

Slot for AP n is zone+n*0x100 (n=1..7); offsets below are slot relative:

| Offset | Bytes | Field / writer |
|---|---:|---|
| 0x00 | 8 | AP commit magic `DGAPS001`, initially zero, written last |
| 0x08 | 4 | Version=1 / BSP |
| 0x0c | 4 | Slot bytes=256 / BSP |
| 0x10 | 16 | Same attempt UUID / BSP |
| 0x20 | 8 | Expected MPIDR affinity, n<<8 (without U/MT/RES1 flags) / BSP |
| 0x28 | 8 | Expected compressed HAL LocalId, n<<8 / BSP |
| 0x30 | 8 | Actual mailbox physical page / BSP |
| 0x38 | 8 | Reserved zero / BSP |
| 0x40 | 64 | Eight snapshot words: mailbox E00,E08,...,E38 / AP |
| 0x80 | 64 | Eight telemetry words: mailbox F00,F08,...,F38 / AP |
| 0xc0 | 8 | Initial TPIDR_EL1 observation / AP caller |
| 0xc8 | 8 | LPB virtual-address observation / AP caller |
| 0xd0 | 8 | Physical AP-ID observation / AP caller |
| 0xd8 | 8 | Raw LocalId observation / AP caller |
| 0xe0 | 8 | Diagnostic status word / AP caller |
| 0xe8 | 8 | Reserved zero / BSP |
| 0xf0 | 4 | CRC32 over slot[0x08:0xf0] / AP |
| 0xf4 | 4 | Bitwise complement of CRC32 / AP |
| 0xf8 | 8 | Reserved zero / BSP |

The five explicit context fields currently mean: OS-saved startup TPIDR_EL1
from mailbox+0x40, LPB VA from mailbox+0x368, physical AP index from MPIDR
affinity1, raw LocalId read from mailbox+0, and the original error/stage encoding
from snapshot E00. The first field is not an inherited AP TPIDR system-register
value. These observations preserve the exact input values. Zero in a translated
field must not be silently interpreted as a successful lookup.

CRC is CRC-32/ISO-HDLC: reflected polynomial 0xedb88320, initial and final XOR
0xffffffff (same result as Python zlib.crc32). Attempt UUID prevents an old
CRC-valid capture from being mistaken for a new attempt.

Current firmware uses flags=1: generate a fresh host build seed and a SHA-256 of
an explicitly named source manifest before each test image. The generated header
and final image are excluded from that hash, avoiding a self-referential image
hash. The source manifest is not a hash of the complete firmware or its compiled
behavior. Preserve the final packaged image SHA-256 separately in the package
manifest. The first eight bytes of the fresh seed plus a 62-bit retained sequence
form the attempt UUID. An earlier valid header with the same seed prefix advances
the sequence; otherwise it starts at one. Sequence exhaustion refuses a new
attempt. Uniqueness is scoped to that image's retained warm-reset chain. Regenerate
the seed after RAM loss or when packaging another test image; do not claim global
uniqueness for repeatedly booting the same image after hard resets.

```powershell
python diagnostic/degas-aplog/generate_identity.py --manifest artifacts/degas/aplog-integration-v1/source-identity.json
```

Run this after the source edits and before compilation, then retain that manifest
with the compiled image. Do not regenerate the identity after building the image.

## Publication and integration contract

1. BSP preserves the previous full 4096-byte slot in the independent console
   before overwriting it. `SerialPortWrite` must report every line fully written;
   a short write aborts initialization and leaves the old slot unchanged. A
   generic header/CRC validator recognizes prior AP attempts as well as the fixed
   canary, and reports the valid AP commit mask. BSP then assigns the current
   source identity/UUID, clears the new buffer and preinitializes the slots.
   Aligned physical writes publish the Linux signature last, followed by a full
   readback comparison. APs never update shared Linux metadata or another slot.
2. BSP preinitializes all AP metadata before CPU_ON. The actual raw MPIDR is
   copied from F08 by the AP; the decoder compares its affinity bits against the
   BSP expectation and preserves the U/MT/RES1 bits in the telemetry output. No
   slot metadata is changed after CPU_ON. Never clear it on a later firmware
   boot before extracting the prior attempt.
3. AP caller verifies the physical access/cache contract before journal access.
   The existing ARM parking contract has MMU, D-cache and I-cache disabled;
   physical slot/mailbox addresses must be reachable in that state. A UC BSP
   mapping does not establish an AP mapping or stage-2 access. If the contract
   is not met, this unchecked leaf must not run. Do not enable/disable caches or
   alter Windows-owned mailbox bytes 16..2047 to make logging work.
4. Each AP publishes once, before acknowledging/branching to Windows. The leaf
   copies firmware-owned snapshot/telemetry, fills context, computes CRC,
   executes DSB SY, writes one aligned 64-bit commit magic, then DSB SY. It
   leaves that slot immutable for the rest of the attempt. The call site must
   preserve the OS branch target and mailbox register arguments.
5. Use the existing mapped watchdog mechanism, with diagnostic reset enabled
   before leaving firmware. OsHandoff.h supports
   `DEGAS_DIAGNOSTIC_EBS_WATCHDOG=1`; the diagnostic branch arms thirty seconds
   at EBS. The DSC/build plumbing selects that branch for an AP diagnostic image;
   its default is disabled. Verify the emitted branch and log marker in the exact
   image before a Windows test.

`ap_journal_commit.S` is the independently tested reference leaf. Its commit body is
exactly **120 bytes including its magic literal**, and its bitwise CRC helper
is **64 bytes**. The caller's argument setup/preservation, guards, alignment and
any branches are additional. It uses no stack, UEFI calls, MMIO, optional ARM
CRC instructions, or architectural-state changes. It preserves x0..x6 and
x14..x29/SP, clobbers x7..x13/x30/flags, and returns via saved x13. The integrated
Parking.S removes the bulky framebuffer hex renderer to make space. Remeasure
the complete relocated blob against its 0x600-byte cap and ensure it ends before
mailbox+0xe00. Do not claim the 120-byte body alone is the entire integration cost.

The immutable slot does not make DRAM nonvolatile. DSB completes uncached
writes; it cannot prevent preloader/LK clearing, power loss, stage-2 faults, or
Android reusing its own dmesg slots during a later panic. This is a small
handoff journal, not a Windows kernel dump or a replacement for crashdump/KD.

## Repeating transport qualification

The first BSP-only transport test passed as described above. Repeat it when
changing the envelope, reset route, stock firmware or ramoops layout. Keep it
separate from new Windows/AP startup changes.

1. Root operator captures existing pstore and record 30, stock boot/image
   identities, current ramoops parameters, and an attempt manifest. Keep the
   original capture immutable on the host. Reconfirm ECC=0 and the stock map.
2. A UEFI-only diagnostic initializes this envelope/header with its recorded UUID,
   recorded identity SHA-256 and informational counter. It leaves
   all seven AP commit words zero; it does not invoke Windows or CPU_ON. Read
   the initialized bytes back and compare header/CRC before arming the already
   implemented ten-second reset-only watchdog. No filesystem service is needed.
3. Let the watchdog reset the device. Follow the known stock recovery/restore
   path without a power-off, long-key hard reset or a second UEFI initialization
   of this zone. Passage through fastboot is part of the path being tested.
4. As soon as rooted Android ADB is available, the root operator collects all
   `/sys/fs/pstore` files as a binary tar and device boot identity. Do this
   before waiting for boot_completed; Android logging daemons may consume or
   remove files. Existing collect_probe_ramoops.py already captures pstore.tar
   with completion trailers. The `/data/debuglogger/console-ramoops-0` copy is
   console-only and can be stale; it cannot prove this dmesg journal survived.
5. Extract `dmesg-ramoops-30` offline and decode it with the manifest's UUID and
   hash. Success means the current private header/CRC/UUID/hash/counter all
   match. Seven `not committed` slots and `complete:false` are expected for this
   transport-only run. Missing or stale data does not prove AP code failed;
   investigate the reset/LK/Android retention path before Windows integration.

The AP-enabled image still needs a distinct hardware test with its diagnostic
EBS watchdog. Collect the same evidence for every attempt. The host decoder is
read-only; it does not clear pstore.

Generic firmware recovery emits raw bytes between CRC-matched
`DEGAS APJ RAW BEGIN` / `DEGAS APJ RAW END` markers in the BSP console. Each
16-byte line has an exact offset. This backup is a complete copy of the **4 KiB
journal slot**, not a RAM dump. Extract it automatically with the expected UUID;
partial, missing-offset, CRC-invalid or conflicting records are refused:

```powershell
python diagnostic/degas-aplog/extract_console_journal.py PATH_TO_CONSOLE --attempt UUID --source-manifest-sha256 SOURCE_HASH --output NEW_RAW_4096_FILE
```

```powershell
python diagnostic/degas-aplog/ap_journal_v1.py PATH_TO_EXTRACTED_RECORD --attempt UUID_FROM_MANIFEST --build-sha256 SHA256_FROM_MANIFEST
python diagnostic/degas-aplog/test_ap_journal_v1.py
python diagnostic/degas-aplog/test_ap_journal_asm.py
python diagnostic/degas-aplog/test_ap_journal_bsp.py
```

The pure-format suite covers raw/export equivalence, independent torn slots,
immutable publication, CRC/complement, UUID/hash rejection, affinity and bounds.
The assembly suite assembles the current source with clang/LLD, executes it in
Unicorn at two code origins for AP1/AP7, compares all zone bytes to the Python
reference, checks register preservation and mailbox immutability, and verifies
the last write is the slot's aligned commit store. It uses the repository's
existing Unicorn dependency under artifacts/degas/cpu-audit/test-deps. On
Windows it uses existing WSL compiler tools and a temporary host build folder.
It does not emulate cache visibility, stage-2, PSCI, warm reset, LK, Android, or
Windows. The separate actual-C BSP helper test cross-checks the codec, sequence
identity, exact raw backup extraction and fail-closed short-write behavior.
Passing host tests does not establish AP hardware capture; transport retention
is supported separately by the recorded device qualification.
