# Degas display, watchdog and RAM observability audit — 2026-10-26

Scope: independent offline inspection of the current Xiaomi 14T / MT6897 Mu source and existing evidence. No phone, ADB, fastboot, flashes, reboots, full firmware builds, or induced exceptions were used by this agent. After the initial read-only audit, the parent explicitly authorized the narrow shared BLT correction, then the guarded opaque-GOP helper and board snapshot protocol, with focused host tests. Those changes are documented below. No Windows/menu code was edited by this agent. No applicable `AGENTS.md` was found in the workspace or its checked ancestors; the 2026-10-24 display handoff was read.

## Later V11 evidence and guarded opaque implementation

The fresh V11 log `evidence/raw/2026-10-26-degas-windows-v11-result/console-ramoops-0` changes the available stage evidence: the large request completes in approximately 14.9 seconds; sampled RGB changes, zero-alpha samples increase to 1518/1536, and the trace reaches `DEGAS EBS core ready-to-return success`. This establishes the firmware reached the final EBS return boundary; it still does not establish Windows kernel entry. The historical V8/V9 distinctions below remain accurate for those runs.

With parent-reviewed masks, `DegasOpaque.h` and `DegasGop.c` now attempt a narrow register-only opaque setup before publishing GOP. Only plain CON format `0x2000` after excluding bits0..8 is accepted on both views. This is the pinned source's non-premultiplied DRM XRGB8888 mapping: the little-endian word `0xXXRRGGBB` occupies bytes B,G,R,X, corresponding to the advertised GOP BGR-reserved layout. Premultiplied/manual format `0x803000`, swaps, flips, other format/control bits, unexpected pitch bits and changed geometry are rejected before writes. Existing source addresses, 4992-byte pitch and geometry are preserved.

The setup writes only CON alpha+AEN mask `0x1ff` to opaque FF and PITCH constant-blend bit28 (`0x10000000`) at `0x14402030/0x14602030` and `0x14402044/0x14602044`. It captures/logs original controls, verifies each write and final four-register state, and restores attempted writes in reverse order if verification fails. Failed rollback blocks drawing. Unsupported CON or a safely restored legacy state retains the prior menu BLT normalization path. Once opaque state is verified, GOP preserves pixel reserved bytes and refuses to silently fall back if the hardware state later changes. Register readback is not a hardware visibility claim.

`Include/Protocol/DegasDisplayRefresh.h` exposes GUID `dcb2a9a4-31a2-432e-95ba-893774bcfe86` with `EFI_STATUS Flush(This)`, installed alongside GOP. After successful opaque setup it checks current controls, geometry and DSI state, cleans cache and issues only the previously proven bounded transfer. It does not call Boot Services, allocate, configure alpha or write pixels. Without verified opaque setup it returns unsupported. This is one diagnostic snapshot for a consumer cached before StartImage; it is not autonomous display support.

`diagnostic/tests/test_degas_gop_opaque.c` includes the actual GOP source with mocked hardware and BLT. It passed strict GCC compilation and verifies exact masks/order, all four write-failure rollback points, rollback failure, format/pitch/geometry rejection, idle timeout, legacy fallback, zero reserved-byte preservation, already-opaque idempotence, and flush with `gBS=NULL`. The snapshot test compares the entire mapped framebuffer before/after and checks every MMIO write belongs to the proven transfer allowlist. Hardware testing remains pending.

## Finding

There is a concrete display handoff deficiency, but there is no captured V9 fatal failure or captured V9 successful ExitBootServices (EBS). The standard console success proves the pre-EBS `ConOut -> GOP Blt -> command-mode DSI refresh` path. It does not validate the linear-framebuffer rendering path advertised to Windows. Direct framebuffer stores bypass both the required transfer trigger and the current alpha repair.

The lack of an on-screen exception, progress message, or automatic V9 reset cannot by itself establish Windows kernel entry. A new retained trace is necessary. V10 is a useful short recovery experiment; its EBS marker must be interpreted as a firmware boundary, not as a Windows boot success marker.

## Evidence baseline

| Observation | What it establishes | What it does not establish |
| --- | --- | --- |
| V8 CONSOLE TEST physically visible; log has `DEGAS STANDARD CONSOLE OUTPUT=Success` at line 108 | The standard text output path reaches the real panel using this image and inherited hardware state | Fault-time output reliability; direct framebuffer visibility; post-EBS refresh |
| Fresh V8 WBM trace loads bootmgfw and ends at cumulative 403247104 bytes, 255680512 bytes remaining, 65666816 us into the large request | Successful active firmware I/O through the recorded end; reset is consistent with the 60-second feed cutoff plus hardware timeout | Exact file identity; completion of that request; EBS; Windows kernel entry |
| V9 debuglogger file SHA256 equals the previous V8 console exactly | The retrieved file is stale V8 evidence | Any V9 performance, exception, EBS, or kernel result |
| V9 manual recovery, empty pstore | Fresh V9 state was not recovered | Why the watchdog did not reset or why logs were lost |

Relevant captures:

- `evidence/raw/2026-10-25-degas-console-v8-result/console-ramoops-0`: lines 107–110.
- `evidence/raw/2026-10-25-degas-windows-v8-result/console-ramoops-0`: lines 490–532. SHA256 `c454db835334d197db209b1f6ef9af38e1f5a62b3fdbd75eea27dab7a8e13073`.
- `evidence/raw/2026-10-25-degas-windows-v9-result/debuglogger-console-ramoops-0`: independently hashed to the same SHA256 above. Its manifest reports 30024 bytes; `state.txt` shows the file timestamp predates that Android session and pstore is empty.

## Display: proven BLT path versus exposed direct framebuffer

1. `mu/Platforms/Xiaomi/degasPkg/degas.dsc:28–31` configures width 1220, height 2712, 32-bit pixels, stride 1248 pixels (4992 bytes). `DegasGop.c:265–279` exposes the real base/size, dimensions and `PixelBlueGreenRedReserved8BitPerColor`. This invites a loader to write the framebuffer directly.
2. `DegasGop.c:113–126` calls `FrameBufferBlt`, then ORs `0xff000000` into each destination pixel, then invokes `DegasDisplayRefresh`. Video-to-buffer readback skips this entire block. No independent periodic refresh, direct-write observer or EBS refresh hook exists in this driver.
3. `DegasGop.c:69–90` validates retained OVL/DSI/mutex values, cleans the full framebuffer cache range, queues all four processing mutexes and issues the single-DSI command transfer. Memory stores alone do not execute this function. Its waits are bounded software loops; a bus access that itself never returns is outside that guarantee.
4. The framebuffer is reserved and mapped `WRITE_THROUGH` (`MemoryMapLib.c:16`); RAM logging is separately uncached (`:15`). Cache visibility is separate from starting a panel transfer. A cache clean alone does not replace the DSI trigger.
5. The inherited OVL consumes byte 3 as alpha; the documented v2 black rectangle and v3 visible green rectangle establish that the current alpha repair matters. A loader can legitimately write a zero reserved byte under the advertised BGR-reserved format. Those direct stores bypass the repair as well as the refresh.
6. `FrameBufferSetMode` at `DegasGop.c:51–56` accepts mode 0 without clearing or refreshing. Therefore seeing the previous starting text persist does not even prove a later caller failed to call SetMode.

The [UEFI GOP specification](https://uefi.org/specs/UEFI/2.10/12_Protocols_Console_Support.html#graphics-output-protocol) describes a directly accessible linear framebuffer and defines byte 3 as reserved for the advertised format. The current inherited alpha interpretation and trigger-on-BLT implementation do not provide equivalent behavior for direct writes.

There is also a specific Windows requirement gap: [Microsoft's SoC UEFI requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/uefi-requirements-that-apply-to-all-windows-platforms) require stride equal to horizontal resolution, a physical BGR-reserved framebuffer, and continued scanout after EBS. Current stride is truthfully 1248 versus 1220. This is a compatibility gap, not proof that the observed WBM execution stopped there. Setting the reported stride to 1220 without changing the actual surface/pipeline would corrupt row addressing and is not a fix.

**Classification:** direct framebuffer display support is incomplete, independently of any Windows fatal failure. It can hide an otherwise advancing loader, an error screen, or later OS output. There is no evidence identifying which of these occurred in V9.

### Deeper GOP interface review and smallest corrections

The current UEFI mode is internally truthful: physical base `0xfcc2f000`, visible 1220x2712, BGR-reserved 32-bit pixels, 1248-pixel pitch, and `FrameBufferSize=13538304` (`0xce9400`). The reserved mapping is `0xcea000`, leaving 3072 mapping bytes after the surface. UEFI permits row padding; this pitch is not by itself a UEFI GOP violation. Microsoft's additional Windows requirement is stricter. Neither fact establishes which path WBM used or why any run stopped.

| Interface | Current implementation and implication | Smallest appropriate change |
| --- | --- | --- |
| QueryMode(0) | Allocates a new info buffer and returns the active dimensions/format/stride. `PixelInformation` is not copied or initialized in that allocation (`DegasGop.c:32–45`). It is ignored for current BGR mode, so this does not establish a current Windows failure. It would be wrong for the advertised helper's 16/24-bit PixelBitMask modes. | Copy the complete initialized mode-info structure. Do not change current pitch or pixel layout in this cleanup. |
| SetMode(0) | Returns success without clearing or refreshing (`:51–56`). This violates the SetMode clear-to-black behavior documented in the local UEFI binding (`MdePkg/Include/Protocol/GraphicsOutput.h:115–141`). Even reselecting mode 0 must implement the documented operation. | Clear the visible rectangle through the existing opaque-black BLT path and propagate transfer failure. No panel timing change is required for this one-mode correction. |
| BLT destination checking | The driver's overflow-safe rectangle checks run after `FrameBufferBlt` (`DegasGop.c:113–120`). The shared library's fill/buffer operations have addition-based checks, so this postcheck does not protect against arithmetic-wrap inputs before a store. | Validate operation-specific source/destination bounds with subtraction before entering the library. This is a robustness fix; malformed WBM input has not been observed. Only the separately authorized VideoToVideo shared-library fix below was made. |
| Reserved byte | The driver forces byte 3 opaque only after BLT, masking inherited per-pixel blending for that path. Direct writers see no equivalent rule. | Make hardware treat the BGRX buffer as opaque independently of byte 3. There is no standard GOP alpha format that can require arbitrary Windows writers to supply FF. |
| Direct framebuffer writes | There is no automatic transfer contract in the driver. SetMode/QueryMode metadata cannot cause command-mode transfers. | Provide a validated autonomous hardware scanout/refresh path that survives EBS, or a suitable OS display implementation. A BLT callback or EFI timer is insufficient after EBS. |
| Protocol installation/error cleanup | `RegisterFrameBuffer` returns success even if installation failed (`:406–412`, after ASSERT); cleanup dereferences `mFrameBuffer.Mode` before checking it (`:338`) if its initial allocation failed. | Preserve installation error and null-check the outer mode before its Info pointer. These are failure-path defects, not an explanation of the successful existing console test. No edits made here. |

The current 16/24-bit branches are not independently usable configurations: alpha normalization and display guards remain hardcoded for the actual 32-bit, 1220/1248-pitch inherited hardware. They should not be presented as validated alternatives merely because the metadata helper has those branches.

The proper opaque BGRX hardware candidate is **constant blending**, not a blind clear of the layer alpha-enable bit. The parallel panel audit found same-SoC source and exact installed-module code setting pitch-register bit 28 for `DRM_MODE_BLEND_PIXEL_NONE`, with constant alpha FF in the layer CON register. See [the panel audit](DEGAS-PANEL-AUDIT-20261026.md). Its sources are the pinned MiCode blob `drivers/gpu/drm/mediatek/mediatek_v2/mtk_disp_ovl.c` at commit `3c4ccd07d5624879555ec710c11bb8818e5cb5c6`, and `evidence/extracted/2026-09-14/display-modules/mediatek-drm.ko`, whose GNU build ID matches the captured installed-driver note. This is a source-backed candidate, not hardware validation. Both processing halves, CON state, masked pitch validation, refresh guards and preserved fields must be treated together. Current capture omits layer CON `+0x30`, so no register write is justified from the pitch value alone.

Meeting the packed Windows stride requirement would require a real 4880-byte surface layout and compatible OVL pitch configuration. Both halves, renderers, cache range, metadata, memory bounds and exact guards must agree. Do not report 1220-pixel stride while the hardware still advances 4992 bytes; do not report width 1248 for a native 1220-wide panel. Those changes alter declarations rather than supplying the promised layout.

The following are diagnostic workarounds, not the smallest proper Windows fix:

- A one-pixel GOP write can refresh unrelated direct stores before EBS, but Windows is not required to make that extra call.
- An EFI timer can trigger refresh before EBS, but stops with firmware timers and cannot own post-handoff rendering.
- A packed RAM shadow buffer plus CPU copy/alpha normalization still needs persistent copy/refresh execution after EBS; a Boot Services callback does not provide that.
- PixelBltOnly can honestly avoid promising direct framebuffer semantics for a firmware-only diagnostic, but Microsoft explicitly excludes it for Windows. The current FrameBufferBlt library also rejects that exposed format unless a distinct private configuration is retained.
- PixelBitMask does not turn a reserved byte into a standard alpha requirement and is not the required Windows BGR-reserved mode.

The smallest proper **local API** improvements are complete QueryMode copying and real SetMode clearing. The smallest proper **display handoff** fix must additionally address opacity and persistent transfer in hardware; its register sequence is not yet established. The two should not be confused.

### Authorized shared-library VideoToVideo correction and verification

The selected library is `MdeModulePkg/Library/FrameBufferBltLib` (`SiliciumPkg.dsc.inc:221`). Its reverse-copy branch previously advanced source and destination by `Height * LineStride` before copying `Height` rows. This starts one row past the requested rectangle. For a valid full-width one-row copy from `(0,0)` to `(0,2711)`, it wrote 4880 bytes at row 2712: outside the reported surface, with 1808 bytes beyond even the mapped reserve. An ordinary downward or same-row rightward copy could also copy the wrong rows. GraphicsConsole's upward scroll uses the forward branch, so the console PASS does not exercise this defect. WBM's use of the reverse operation remains unobserved.

With explicit parent authorization, only `FrameBufferBltLibVideoToVideo` was changed: reverse copying starts at `(Height - 1) * LineStride`, and its four source/destination extent checks now use subtraction after checking the starting coordinate. The latter rejects addition-wrap inputs before address arithmetic. Other library operations were not changed.

`diagnostic/tests/test_degas_framebuffer_video_copy.c` includes the actual selected library source and calls its actual VideoToVideo function, using host `memmove` only as the platform CopyMem implementation. Expected output is calculated from an immutable snapshot of the original surface, independently of copy direction. Every word of the surface, row padding, and outer canaries is compared. It covers last visible row, downward/upward row overlap, rightward/leftward same-row overlap, bottom-row horizontal overlap, identical full rectangle, integer-wrap coordinates, exceeded edges, and zero extents.

- Before the production change: GCC built the test, which failed `last visible row` at word 114 (the requested final row was unchanged).
- After the change: the same test passed with `-Wall -Wextra -Werror`.
- No full firmware build, packaging or phone test was run by this agent. This correction removes a reproducible memory/copy defect; it does not establish that it caused the observed WBM symptom.

Reproduce from the workspace inside WSL, placing the executable in a temporary directory:

```sh
gcc -std=c11 -Wall -Wextra -Werror -fshort-wchar \
  -ffunction-sections -fdata-sections -Wl,--gc-sections \
  -I mu/Mu_Basecore/MdePkg/Include -I mu/Mu_Basecore/MdePkg/Include/X64 \
  -I mu/Mu_Basecore/MdeModulePkg/Include \
  diagnostic/tests/test_degas_framebuffer_video_copy.c -o /tmp/degas-framebuffer-video-copy-test
/tmp/degas-framebuffer-video-copy-test
```

For later GOP-only changes, a focused host harness should also verify QueryMode returns independent complete info, valid SetMode clears only visible pixels and triggers refresh, invalid modes make no writes, all four BLT operations preserve correct rows/padding, and guard/timeout failures propagate. Those are separate from the hardware visibility experiment below.

## Why an exception or monitor can remain silent

- `WindowsBoot.h:33–35` defines `WindowsFeed` as a call to `BootWatchdogFeed`; it emits no text or RAM heartbeat. `:74–78` installs a one-second timer and an EBS event. This is timeout recovery, not a liveness monitor that diagnoses the current execution stage.
- `UfsBlockIo.h:39–65` raises TPL to `TPL_NOTIFY` for the complete synchronous request. A `TPL_CALLBACK` timer feed cannot provide regular callback telemetry inside that request. The explicit chunk feed at `:57` supplies recovery instead. The progress record at `:53–55` is RAM logging only.
- The selected DEBUG library is `BaseDebugLibSerialPort` (`SiliciumPkg.dsc.inc:164`). `DebugAssert` formats and sends its message to `SerialPortWrite` (`DebugLib.c:220–227`); there is no `ConOut` call there. A firmware ASSERT need not appear on the panel.
- The AArch64 default exception handler writes its initial exception to RAM first (`DefaultExceptionHandler.c:198–211`). It then performs extensive dump work, including dereferencing stack locations (`:318–329`), before attempting `ConOut` (`:332–336`). A recursive exception logs a short RAM message and deadloops before that screen output (`:198–201`). Fault-time console success is therefore weaker than the harmless CONSOLE TEST success.
- The handler does not install or own the OS's exception vectors. After handoff, Windows exceptions are not guaranteed to pass through this firmware handler.
- EBS clears `ConOut`, `StdErr`, boot services, and their handles (`DxeMain.c:853–870`). It also stops the firmware timer. Standard text output and Boot Services timer telemetry cannot be relied on after that transition.

Consequently, neither “no visible synchronous exception” nor “starting text never changed” proves the CPU was not executing or that no failure occurred. Conversely, silence is not evidence that an exception did occur.

## Watchdog interpretation

The MMIO watchdog and the generic UEFI watchdog are different mechanisms.

- `BootWatchdog.h:10–16` writes length `0x5008`, sets EN, clears IRQ/DUAL bits, and reloads with `0x1971`. Existing hardware traces read length `0x5000` because the write key is not retained. The saved MediaTek source `evidence/sources/degas-buttons/mtk_wdt.c:33–49,178–212,230–263` supports the timeout encoding, keys and enable/reset-only bits.
- `BootWatchdog.h:4–7` stops reloads when measured elapsed time reaches 60 seconds after `WindowsBoot.h:48`. The one-second callback and BlockIO chunk feed share this cutoff. Before EBS, the intended recovery is about 60 seconds plus up to the ten-second hardware timeout. It is not a two-minute independent monitor.
- Normal `OsHandoff.h:28` explicitly disables the hardware watchdog by clearing EN. Thus EBS is a plausible explanation for the V9 no-reset observation. It is not established because no fresh V9 EBS marker or watchdog readback survived.
- V10 enables `DEGAS_DIAGNOSTIC_EBS_WATCHDOG` in `Checkpoint.c:210`. In that branch `OsHandoff.h:23–26` reprograms/reloads the ten-second reset timer after the UFS shutdown. It deliberately resets even if Windows succeeds. It is a diagnostic, not a production OS handoff policy.
- `WatchdogTimerDxe/WatchdogTimer.c:150–160,223–228` implements the architectural UEFI watchdog with an EFI timer. `BmBoot.c:2144,2160,2186` arms/cancels it around StartImage. These operations do not target MT6897 watchdog MMIO and must not be presented as proof that WBM disabled the hardware watchdog.
- Disabling EFI timer interrupts or delaying a timer callback alone does not explain a no-reset hardware watchdog that remains enabled and running; it normally removes feeds. Alternative claims about clock gating, secure-world interference, a stopped performance counter, or another writer require new evidence. None is established here.

The precise EBS boundary matters. Local `CoreExitBootServices` disables the timer at `DxeMain.c:809`, validates/terminates the memory map at `:814`, and only signals `gEfiEventExitBootServicesGuid` at `:828` after that succeeds. Our `DEGAS WINDOWS EXIT BOOT SERVICES` marker (`WindowsBoot.h:38`) therefore establishes delivery of that successful-map-termination event. It does not establish that every later EBS callback completed, that EBS returned to the loader, or that the Windows kernel started. A failed map-key attempt can already stop timer telemetry without delivering our EBS event.

`DegasOsHandoff` is idempotent (`OsHandoff.h:7–8`). It marks the transport inactive, stops IE/UTRL/UTMRL/HCE, polls HCE for up to 1000 microseconds, then changes the watchdog (`:9–28`). The early EBS marker precedes those accesses; the reset-armed marker follows the programming writes; final `DEGAS HANDOFF` includes HCE and mode readback (`:30–32`). In V10 an early marker alone must not be reported as successful cleanup or confirmed watchdog arming. The host handoff test validates register sequence/idempotency, not real reset delivery or every cleanup failure mode.

## RAM log limits

`RamSerialPortLib.c:56–63` appends to a 262132-byte payload at `0x480af00c`, conditional on DBGC signature and plausible start/size. It returns 0 silently on an invalid header and does not initialize or repair it. Wrapper initialization is separate (`diagnostic/ramoops_probe.c:54–55`). The data and header are followed by barriers, but there is no per-message CRC, independent progress slot, timestamp, or writer lock.

The ring can overwrite its oldest milestones. V4 already demonstrated this; V8's 30024-byte retained trace is small enough that ring flooding is not the explanation for its missing EBS marker. Interrupted/nested writers can race the shared indices; the code provides no reservation or reentrancy protection, although that is not established as the cause of V9 loss.

Ramoops recovery requires retained contents and a valid Linux header. The saved primary kernel source validates the signature and start/size before saving old contents (`evidence/sources/ramoops/ram_core.c:518–540`). It does not make arbitrary resets preserve DRAM. An empty pstore plus an unchanged debuglogger fallback is missing evidence, not a new console capture. A run-specific identifier in the next diagnostic trace would make stale-copy rejection immediate.

## Minimal next experiment, prepared offline only

Two questions should be answered independently. Do not combine a new display mode/panel reconfiguration with the first recovery trace.

**1. Fresh handoff trace using the already built V10 diagnostic, when hardware testing is authorized.** Keep its short existing recovery policy. Accept only a new run/build identifier and capture hash; reject the previous V8 hash. Look for the last BlockIO progress, the EBS event marker, `DIAGNOSTIC EBS RESET ARMED`, and final handoff readbacks. Report their presence separately. An EBS marker plus reset-armed/readback and automatic reset sharply narrows the pre/post-EBS boundary, but still does not prove kernel entry. No marker in a fresh complete trace means that EBS event was not observed, not that no EBS attempt occurred. A reset with no fresh log leaves the stage unresolved.

**2. A tiny standalone direct-framebuffer visibility probe, independent of Windows.** This is a design, not implemented or built by this audit:

1. Arm the known ten-second hardware reset; log unique probe ID, geometry, framebuffer base, mode and WDT readbacks. Reuse current guards and existing framebuffer allocation. Avoid UFS, CPU exceptions and display timing/power changes.
2. Use the existing GOP path once to establish a black panel. Then directly write two bounded 96x96 white rectangles with identical RGB, one with byte 3 zero and one with byte 3 FF, in visible coordinates such as `(100,600)` and `(300,600)`. Honor 4992-byte pitch; leave padding and all other pixels unchanged. Log/read back both rectangles and clean cache before the observation interval.
3. Wait about two seconds with **no GOP drawing anywhere**. RAM logging is fine. Observe whether either direct rectangle appears. A text/status update via GOP during this interval would invalidate the test by triggering refresh.
4. Trigger one GOP BLT pixel outside both rectangles. The existing implementation normalizes only that pixel's alpha but flushes/transfers the whole surface. Observe the same rectangles for another two seconds. Do not use a BLT over either test rectangle: that would repair the very alpha property being tested.
5. Return to the existing reset wait without feeds. Require automatic recovery and a new retained trace with the probe ID.

Interpretation: if neither rectangle appears until step 4, the direct path lacks automatic transfer. If after step 4 only the FF rectangle appears, zero reserved-byte transparency is independently demonstrated. If FF appears during step 3, some automatic transfer mechanism is operating and that narrower result should replace the trigger hypothesis. Readback/CRC verifies CPU stores, not optical output.

Offline acceptance tests for any later implementation should mock the framebuffer and MMIO separately, assert no trigger during the direct interval, verify only visible rectangle bytes change, assert the one-pixel BLT leaves both rectangle alpha bytes intact, check guard rejection and bounded waits, and verify WDT register encoding. A host test cannot validate actual panel timing or physical visibility.

After a candidate autonomous hardware path is validated before EBS, its final acceptance test should be a standalone loader with no Windows or UFS: save all needed physical addresses before exiting Boot Services, perform the usual valid-memory-map-key EBS transition with bounded retry, then alternate bounded direct-written RGBX rectangles using a counter delay and the already armed hardware reset. Make no GOP, Boot Services, or firmware timer calls after EBS. Require several successive visible post-EBS changes and an automatically recovered run-specific RAM trace. A test stub that manually reissues DSI triggers after each paint proves the hardware sequence can still work, but does **not** prove autonomous framebuffer refresh. Test both zero and FF reserved bytes once the opaque hardware configuration is the candidate under test. This later test is a design only; it is not authorization to enable an unverified display mode or perform a phone test now.

A permanent Windows-compatible solution needs framebuffer behavior that survives the firmware handoff and does not depend on Boot Services BLT callbacks. Investigate a validated autonomous hardware command-queue/refresh path, a validated video-mode configuration, or an explicit OS display implementation. None of those hardware paths is established by the current driver, and none should be enabled from generic register assumptions. A firmware periodic callback is at most a pre-EBS diagnostic; it stops at EBS. Periodic firmware painting/whole-surface alpha rewriting after handing the buffer to Windows would also violate ownership and can damage Windows output.

### Review of a proposed read-only framebuffer observer

An offline follow-up may sample the framebuffer in `WindowsFeed`, establish a baseline after the starting text returns from ConOut, and take a final sample in the EBS callback. This can discriminate framebuffer mutation from unchanged panel contents without changing the display contract. Use bounded aligned scalar volatile reads, no allocations, no GOP calls, no framebuffer writes and no cache-maintenance side effects. Record sampled pixel count, sample number and elapsed time; label zero-alpha counts as sample counts rather than full-surface statistics. Separate RGB and alpha changes if practical.

The callback's requested two-second observation period is not a delivery guarantee: a large BlockIO request at TPL_NOTIFY suppresses the TPL_CALLBACK observer. No callback record during that interval is expected. An unchanged sampled hash cannot exclude rendering elsewhere or transient same-data writes. A changed hash establishes memory mutation, not visible scanout, DSI completion, or kernel entry. An EBS sample taken before `DegasOsHandoff` reports pre-handoff WDT state; the final handoff readback reports post-programming state. Keep those labels distinct.

## Conclusions to carry forward

- Proven: standard pre-EBS console works; V8 reset occurred during active reads; direct framebuffer semantics are incomplete in the source.
- Unproven: V9 EBS, V9 copy/full-load success, a V9 fatal exception, Windows kernel entry, and the cause of the missing V9 log.
- Do not equate a recovered EBS event with kernel success, a frozen panel with a dead CPU, or a stale console file with a new run.
