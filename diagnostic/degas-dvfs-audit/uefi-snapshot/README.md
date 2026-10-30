# Read-only UEFI CSRAM snapshot

Read-only CSRAM observation core and offline decoder. Firmware integration supplies MMIO mapping, menu entry, build identity and retained RAM logging. Cache probing and controller writes are outside this helper.

## Capture contract

`UefiCsramSnapshot.h` includes only `Uefi.h`. `DegasDvfsCaptureUefi` requires two
callbacks: qualify an exact aligned DWORD without loading hardware, then read
that exact DWORD. It qualifies **all 109 addresses before the first read** and
stops at the first failed qualification/read. There is no polling, allocation,
Boot Services use, direct hardware access, or write callback in the core.

The strict allowlist is derived from the captured DT CSRAM resource
`0x0011BC00/0x1400`; it is smaller than either that resource or its mapping:

| Slots | Exact reads |
| --- | --- |
| 0 | Global mode `0x0011C0CC` (`CSRAM+0x4CC`) |
| 1..36 | Domain 0 at `0x0011BC10`: LUT+0..0x7C, enable+0x84, request+0x88, readiness+0x8C, latency+0x114 |
| 37..72 | Same offsets at domain 1 `0x0011BD30` |
| 73..108 | Same offsets at domain 2 `0x0011BE50` |

The callback receives only allowed addresses. It must read one aligned 32-bit
word using the platform's Device-memory read primitive and return FALSE when
qualification or access cannot be completed. It must not enable DVFS, write the
enable/request words, clear logs, initialize MCUPM, or perform read/modify/write.
`AT S1E1R` may help verify the current EL1 mapping, but does not prove Stage 2
permission or guarantee that EL2 will not trap the eventual access. The core is
not a data-abort catcher. Qualification/access policy remains an integration
requirement; a failed qualifier prevents every hardware read.

`DEGAS_DVFS_SNAPSHOT` is 476 bytes with the checked host/ARM64 field layout. It
preserves raw words, exact validity bits, stage, sequence, successful read count,
and first failure slot. Invalid words remain zero, never a callback's failed
output. Status codes are 0 complete, 1 bad argument, 2 qualification failed,
3 read failed, 4 invalid stage. Failure slot 109 means no failed hardware slot.
Stage IDs are 1 DXE, 2 UEFI menu, 3 before OS loader, 4 before ExitBootServices.
Choose the real invocation phase; do not relabel a UEFI capture as Windows.

## Memory map integration recommendation

The inspected platform source
`mu/Platforms/Xiaomi/degasPkg/Library/MemoryMapLib/MemoryMapLib.c` has no CSRAM
entry in the reviewed baseline. The exact resource starts inside the 0x11B000
page and ends at the 0x11D000 boundary. The page-rounded addition is:

```c
{"CPU DVFS CSRAM snapshot",0x0011b000,0x2000,
 AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE},
```

`MemoryInitPei` uses region Address as both PhysicalBase and VirtualBase,
region Length as mapping length, and ArmAttributes directly. `NS_DEVICE` is
the platform alias for `ARM_MEMORY_REGION_ATTRIBUTE_DEVICE`. Page rounding is
therefore deliberate; passing the unaligned DT resource as a mapping entry is
unnecessary. The helper still rejects every surrounding mapped word outside
the 109-word allowlist. AddDev/MmIO is an MMIO resource, never allocatable RAM;
this is a UEFI mapping and does not install a Windows runtime MMIO service.
Device attributes alone do not enforce the no-write policy: callback review
does. The mapping is supplied by platform integration.

## Journal integration

`UefiCsramJournal.h` adds `PrintLib` and formats the completed or partial capture
through a caller-owned emitter. The emitter can use
`DEBUG((DEBUG_ERROR,"%a",Line))` in the existing retained RAM journal. Keep the
matching firmware build ID in that journal/session. No binary record format or
new retention mechanism is required.

```c
STATIC BOOLEAN EFIAPI Qualify(VOID *Context, UINT64 Address);
STATIC BOOLEAN EFIAPI ReadWord(VOID *Context, UINT64 Address, UINT32 *Value);
STATIC VOID EFIAPI Emit(VOID *Context, CONST CHAR8 *Line) {
  (VOID)Context;
  DEBUG((DEBUG_ERROR,"%a",Line));
}
// Storage and sequence ownership belong to the caller.
DegasDvfsCaptureUefi(Qualify,ReadWord,Context,DegasDvfsStageMenu,Sequence,&Snapshot);
DegasDvfsJournalUefi(&Snapshot,Emit,Context);
```

Each snapshot has 18 lines: begin/source/stage, global mode, three domain
metadata lines, twelve eight-word LUT lines, and matching sequence end. The
tested complete example is 1461 bytes, every line below 192 bytes. Domain
validity is a 36-bit hexadecimal mask (`%09Lx`); bits 0..31 are LUT validity and
bits 32..35 are enable/request/readiness/latency validity. The sequence end lets
the host reject an overwritten or truncated journal. All-zero successfully read
words remain distinct from unread words through validity bits.

## Offline decode and classification

```powershell
python diagnostic/degas-dvfs-audit/uefi-snapshot/decode_snapshot.py retained-journal.txt -o snapshot.json
```

The decoder requires complete, nonduplicate DVF1 frames with matching sequence
and consistent validity/counts. It rejects relabelling the source as Android or
Windows. It retains every raw LUT word, extracts low 12 bits as MHz, and reports
the vendor driver's frequency list in kHz, ending at the first consecutive
repeated frequency. Missing/empty/non-descending LUTs are not used to decode
requests. Mode 0 selects LUT-index requests; nonzero global mode selects the
source driver's kHz-request path. The alternative FDVFS path cannot be excluded
by that mode word alone. A request is reported only within the qualified table
or range and is explicitly **not a measured/accepted physical clock**. Readiness
bits 0 and 1 are separate CPUFREQ/SVS state observations. Latency is a reported
microsecond value, converted with a wide host integer to nanoseconds; raw zero
is unknown, not a measured zero-time transition.

Capture reads are sequential, not atomic. Available OPPs, boot requests,
readiness, and transition latency do not establish a Windows physical clock,
thermal ceiling, or working runtime DVFS. An Android mode/FDVFS/readiness probe
is a separate source and phase, even when its values agree. The illustrative
`out/synthetic-only-example.*` files are generated host fixtures, never phone
measurements.

The interpretation source is the local primary reference
`artifacts/degas/cpu-audit/mediatek-cpufreq-hw_main.c` (`LUT_FREQ`, offsets,
`mtk_cpu_create_freq_table`, `mtk_cpufreq_hw_get`, probe mode and latency
conversion) and the captured DT in
`evidence/raw/2026-10-25-degas-ufs-baseline-v2/live-fdt.tsv`. Exact installed-module
findings are recorded separately; the reference source is not a byte-for-byte
match to the installed module.

## Host validation

```powershell
wsl -- bash -lc 'cd /mnt/d/Edk2-For-Xiaomi14T && python3 diagnostic/degas-dvfs-audit/uefi-snapshot/test_snapshot.py'
```

Seven tests pass, including 231 executions of the **actual C core**, compiled
with `-Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=undefined`.
They cover every mapped byte plus extreme invalid addresses, all 109 possible
qualification failures (zero hardware read callbacks), all 109 possible read
failures (no subsequent read; failed output never published), argument/stage
gates, all successful reads, actual C formatter-to-Python decoder round trips,
index/kHz/MHz/us/ns units, and truncation/duplicate/source/count/mask rejection.
`out/host-tests.json` records source hashes. The test's PrintLib stub implements
the used `%Lx` dialect through host `vsnprintf`; this validates the formatter's
output contract, not the firmware's real PrintLib implementation or MMIO access.
