# Xiaomi 14T: boot and framebuffer investigation, 2026-10-26

## V12 hardware success: Windows logo displayed

The user saw the Windows logo replace the starting text, followed by the
intentional ten-second watchdog reset. Fresh capture:
`evidence/raw/2026-10-26-degas-windows-v12-result/console-ramoops-0`, 172,554 bytes,
SHA256 `f5575c471fd4d6aa126dd9790a3dbc2a5e8058488301cdaf522ffeedb672d984`.
Pstore and debuglogger copies match; stock boot_b restored for Android capture.

Both retained OVL CON registers were 0x000021ff and PITCH 0x00001380. Guarded
constant-blend setup succeeded, changing PITCH to 0x10001380. Normal GOP drawing
with reserved byte zero also remained visible. The RAM snapshot is complete
and checksum verified (FNV 45D248A2); decoded `boot-frame.png` contains the blue
Windows mark. EBS at 18.599 seconds, final-frame flush Success, then all core
EBS milestones through ready-to-return success are present.

This confirms a hidden Windows frame and successful explicit delivery. It does
not prove kernel entry or continuous display updates. Keep the opaque fix;
do not mistake the single final flush for autonomous command-mode refresh.

V13 read-only GCE inventory has now passed: power and clocks are retained,
GCTL is 0x20002 and all 32 channels are disabled. Its fresh capture is in
`evidence/raw/2026-10-26-degas-gce-v13-result`; stock boot_b was restored.
Command-fetch mapping remains unresolved. Exact saved ATF and GZ analysis is
recorded in DEGAS-GCE-FETCH-CONTROL-20261026.md and DEGAS-PANEL-AUDIT-20261026.md.
Next is the read-only 0xbb00ffa4 hypervisor capability query in V14, not a
command submission or table initialization. A boolean query response will not
prove a usable DMA mapping. The DMA aperture/IOMMU path differs from UFS.
The unlinked one-frame prototype must
not be enabled using unproven readiness flags. Candidate normal threads are
0..6 only; an earlier offline draft also allowed 8/9, since corrected because
the secure CMDQ header reserves threads 8..12.

## Fresh V11 result — supersedes the earlier unknown boundary

`evidence/raw/2026-10-26-degas-windows-v11-result/console-ramoops-0`
is a fresh 33,669-byte capture, SHA256
`063508b26bfda1beaffa3f25b6cae27d64924affd80d5d30b8138bd1ee70d2b8`.
The pstore and debuglogger copies match. Stock boot_b was restored and Android
booted for collection; no manual reset erased this attempt.

- The large WIM request completed successfully: 647,626,752 bytes in 14,901,273
  microseconds, about 41.45 MiB/s including the CPU copy. This is distinct from
  the approximately 193 MiB/s transport-only repeated-sector test.
- At 17.25 seconds, the sparse RGB hash changed from F5EBD5C5 to 659C09D1.
  Alpha/reserved-byte samples changed from 1,536 opaque to 1,518 zero and 18
  opaque. The same new sample remained at the EBS event, at 20.526 seconds.
  Windows changed framebuffer memory while the user still saw our old text.
- Before-EBS, EBS event, UFS stop (UTRL/UTMRL/HCE all zero), all core callbacks,
  memory-protection completion and **core ready-to-return success** are present.
  Thus this attempt did not stop in bootmgfw's UFS read or an EBS callback.
  This last firmware breadcrumb does not prove kernel execution.
- Ten-second diagnostic watchdog was armed at EBS. Approximately 20.5 seconds
  to EBS plus ten seconds to reset agrees with the user's perceived 20–30
  seconds. The watchdog duration was not ten seconds from menu selection.
- SMBIOS2/3 absent; GetTime returned success with invalid day zero; conventional
  memory before StartImage was 1,125,634,048 bytes. These are still independent
  platform gaps, not established causes of the unseen post-EBS outcome.

Next display work: correct hardware opaque BGRX semantics and validate an
explicit bounded flush of the Windows framebuffer. A final pre-EBS flush can
expose the prepared frame but is only a diagnostic: autonomous post-EBS refresh
still requires the separate GCE/command-mode bridge. Keep this distinction in
hardware results and user-facing claims.

## Earlier boundary (historical V8/V9 evidence)

The last **fresh** Windows-attempt trace is V8. `bootmgfw.efi` was loaded and
issued a large, successful UFS read. At 65.7 seconds the cumulative count was
403,247,104 bytes, with 255,680,512 bytes still outstanding in that request.
The watchdog ended this attempt. This is not evidence of a CPU exception.

V9 changed the CPU copy loop. The user saw no automatic reset for more than
five minutes; manual reset lost the fresh RAM record. The debuglogger fallback
is byte-identical to V8. V9 performance and kernel entry are unmeasured.

## Two investigations

1. **Framebuffer:** The proven ConOut/GOP path explicitly transmits a frame
   through the command-mode panel. Windows direct stores have no corresponding
   trigger, and zero reserved-byte pixels can be interpreted as transparent.
   No autonomous transfer exists in this firmware. A frozen displayed frame
   can therefore conceal a changing framebuffer. V11 now directly observed
   this mismatch during Windows loading; see the fresh result above.
2. **Handoff:** BCD paths and the ARM64 WinPE image are coherent. The standard
   loader path, synchronous UFS completion, and shared EBS implementation have
   no demonstrated unconditional fatal defect. Our EBS event runs before some
   older Timer/GIC callbacks. Disabling the watchdog in that event can hide a
   subsequent firmware handoff failure; it does not prove kernel entry.

The platform exposes about 1.08 GiB of conventional RAM; full-loader headroom
is unmeasured. SMBIOS is absent, and the inherited RTC stub can return day zero.
Those are real platform shortcomings, not proven explanations of this hang.

## Corrections and observations prepared in V11

- Fixed shared FrameBufferBltLib reverse VideoToVideo copy: last row is
  `Height - 1`, not `Height`. The original wrote beyond the requested region
  and could overrun the framebuffer. Actual-library regression failed before
  and passed after the correction. No evidence yet that Windows used this path.
- QueryMode copies the whole initialized mode structure. SetMode(0) now clears
  the visible display through GOP instead of returning success without clearing.
- Serialized the retained RAM serial sink against IRQ-driven writers on the
  single active CPU. Original interrupt state is preserved; no Boot Services.
- Corrected the successful large-read completion logging condition.
- Added read-only, sparse RGB/alpha framebuffer observations before bootmgfw,
  in the existing timer callback and at the EBS event. No framebuffer writes,
  GOP calls, allocations or disk operations in the observer.
- Added preflight free RAM / largest extent / low RAM counts, SMBIOS GUID
  presence and RTC return values. These are diagnostics, not rejection gates.
- Added before-EBS and failed-EBS event markers, plus degas-only core markers
  after all EBS event callbacks, after memory protection, and just before return.
  They do not replace, hook or change CoreExitBootServices ordering.
- V10 diagnostic policy retained: rearm the hardware watchdog for ten seconds
  at EBS. This deliberately resets even a successfully started OS. Before EBS,
  feeds stop at sixty seconds; the nominal reset is about seventy seconds.

### Interpretation limits

Large BlockIO requests hold TPL_NOTIFY, so timer observations can be delayed.
A changing sample proves memory mutation, not visible output or kernel entry.
An unchanged sparse sample does not exclude changes between sampled pixels.
`core ready-to-return success` establishes the last firmware EBS boundary; it
does not prove that winload's next instruction or the kernel completed.

The framebuffer really has stride 1248 and visible width 1220. Do not make the
reported stride falsely equal to the width. A proper autonomous command-mode
bridge needs a validated GCE/CMDQ program and retained DMA ownership; a UEFI
timer is not post-EBS display support. See the panel audit for the exact binary
and source evidence, including why Android's dirty-token loop cannot simply
be transplanted unchanged.

## Checks completed off-device

- Frame sample: bounds, last visible pixel, RGB/alpha separation, padding,
  no writes, and explicit sparse blind spot.
- Actual BLT library: downward/upward and horizontal overlap, last row,
  full rectangle, padding, canaries, overflowing coordinates.
- RAM serial: pending IRQ sees committed outer record, masked IRQ retained,
  wrap, invalid-header rejection.
- Current BlockIO: 14+14+1 batches, all caller alignments, complete output and
  canaries, last LBA, invalid requests, read-only writes, first/second failures.

## Persistent file journal (requested, not implemented in V11)

Use an Android-preallocated file on the verified ESP, two alternating data
slots and checksummed commit records. Keep general BlockIO write-protected;
the private writer must verify the GPT identity and current FAT file allocation
before permitting writes only to that file's extents. First validate a bounded
WRITE(10), cache synchronization, readback and subsequent Android readback.
Then add snapshot saves before bootmgfw and after an error return. Do not write
from the RAM serial sink, exception handler, EBS event, or after EBS. Preserve
the last fully committed slot if a reset interrupts the next write. This cannot
guarantee preservation of the final, uncommitted messages or provide a Windows
kernel logging transport.

Reports: DEGAS-BOOT-AUDIT-20261026.md, DEGAS-DMA-AUDIT-20261026.md,
DEGAS-DISPLAY-WATCHDOG-AUDIT-20261026.md, DEGAS-PANEL-AUDIT-20261026.md.

## V12 implementation and off-device checks (hardware result above)

- Both retained OVL views get constant opaque blending only if the existing
  format is exactly the source-derived plain BGRX format (CON excluding low
  alpha bits = 0x2000), with existing geometry/addresses/idle guards. Changes
  affect only CON low nine alpha bits and PITCH bit28; pitch remains 4992 bytes.
  All writes are read back; a partial failure rolls back and checks restoration.
  Unsupported formats retain the working GOP-only legacy workaround.
- Verified opaque hardware stops treating GOP Reserved as alpha, so the pixel
  values written by Windows need no CPU mutation. Hardware effectiveness still
  needs this phone test; register readback and host tests alone cannot prove it.
- A private board Flush protocol is located/cached before StartImage. At EBS,
  recovery is armed, a framebuffer thumbnail saved, then one bounded transfer
  of the Windows-owned frame is performed. Flush uses no Boot Services, pool
  allocation, GOP BLT, framebuffer stores or memory-map changes. The handoff
  audit found no contract blocker. Three idle waits total at most about 300 ms.
- Thumbnail: 122x272 RGB565, 138,517 ASCII bytes, complete-row order plus FNV
  validation. This and the approximately 34 KiB boot log fit the 256 KiB RAM
  ring. Decoder refuses truncation, corruption and a newer incomplete capture;
  it does not silently substitute an old complete frame. It is a small image
  for identifying the prepared boot frame, not a readable full-resolution BSOD.
- Tests execute the actual GOP source with mocked registers and gBS=NULL for
  Flush; cover rollback at every write, unsupported formats, reserved-byte
  preservation, and busy/geometry failures. The actual C thumbnail routine ran
  against a read-only mmap framebuffer with canary padding, then its decoder
  checked known colors/edges and rejected corrupted/incomplete streams.

The GCE one-frame prototype is independent offline work, NOT linked into V12.
V12 does not claim continuous refresh after EBS or a fully working Windows boot.
The deliberate 10-second post-EBS reset remains enabled.

## V14 query result / next display step

Complete V14 RAM log confirms CurrentEL=4 and type2 query 0xbb00ffa4 returns 1.
All GCE channels remain disabled. Capture and hash are recorded in
DEGAS-DISPLAY-HANDOFF-20261024.md. Stock Android has been restored.
Next staged test is an owned, single-page IOMMU mapping without command-engine
execution. It reserves its tables, preflights the full 1 MiB synchronization
granule in both banks, and resets after verification; no Windows chainload is
allowed after changing the guest association. Continuous refresh remains
unproven. Do not equate type2 query success with DMA or display-loop success.

V15 hardware mapping PASS confirmed by complete 2026-10-27 RAM capture:
HW root7bf00000, mapping query44451a08 -> PA44451000, unchanged marker pairs.
V16 prepares an identity-IOVA internal-SPR sentinel; no display instructions.
See display handoff report for exact scope, encodings and tests.
