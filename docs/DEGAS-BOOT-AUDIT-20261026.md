# Degas Windows boot audit — 2026-10-26

Offline review of Mu boot code and saved display/boot captures.

## Conclusion

**Updated after fresh V11 evidence:** `evidence/raw/2026-10-26-degas-windows-v11-result/console-ramoops-0:559-570` shows the large 647,626,752-byte request completed successfully in 14.901273 seconds, successful EBS notification, empty UFS doorbells/HCE stopped, all EBS callbacks complete, memory-protection completion, and the final `ready-to-return success` breadcrumb. The previous possibility of a hang in the instrumented firmware EBS body is no longer the leading explanation for this run. The final breadcrumb is immediately before return; it still does not independently prove Windows kernel entry.

V11 also observes framebuffer RGB changing while 1518/1536 sampled pixels have zero alpha and the user still sees the previous panel text. That is concrete evidence favoring the display/scanout path. It does not prove what the hidden rendered screen contains or that WinPE reaches its shell. The next bounded diagnostic is a final cached board-specific frame flush at the successful EBS callback, while preserving the short recovery watchdog. It is a one-frame observation, not continuous refresh or a Windows display driver.

The earlier sections below retain the V8/V9 audit and its historical hypotheses so their reasoning remains reviewable. V8's watchdog cutoff during active I/O remains explained by slow copying; V9's recovered log remains stale. Neither should override the newer V11 stage evidence.

The platform also has genuine unfinished Windows firmware requirements, especially physical-framebuffer presentation, missing SMBIOS, and an invalid dummy RTC date. These should be tracked as concrete defects without claiming that any one caused the observed V8 reset or unknown V9 failure.

## Initial V8/V9 evidence and stage boundaries (historical audit baseline)

- `evidence/raw/2026-10-25-degas-windows-v8-result/console-ramoops-0:72-78`: PSCI query and ACPI installation/readback succeed; published XSDT has FACP, APIC, GTDT, with DSDT reached through FADT.
- Same log `:489-501`: 96 GPT children retained, actual ESP FAT filesystem connected, standard console succeeds, `EfiBootManagerBoot` starts, bootmgfw relocated at `0x42fde000`.
- Same log `:503-532`: large request progresses from 16,805,888 cumulative bytes to 403,247,104; last record still has 255,680,512 request bytes outstanding at 65,666,816 microseconds. Successful packet records continue, without ASSERT, exception, or EBS marker. This is active I/O, not an established OS hang.
- Same log SHA-256 is `c454db835334d197db209b1f6ef9af38e1f5a62b3fdbd75eea27dab7a8e13073`. `evidence/raw/2026-10-25-degas-windows-v9-result/manifest.json` records that same SHA for its 30,024-byte debuglogger copy and a failed live-console read. Treat V9 performance and stage as unknown.
- `BootWatchdog.h:4-7` stops feeds after 60 seconds; `:10-17` programs a 10-second reset. V8 timing is consistent with that deliberate cutoff while the large BlockIO call remains in flight.
- `WindowsBoot.h:36-39` prints the EBS marker inside an event callback. Shared `Mu_Basecore/MdeModulePkg/Core/Dxe/DxeMain/DxeMain.c:809-828` validates/terminates the memory map before signaling this event; `:838-882` still performs memory-protection work, disables CPU interrupts, clears boot-service pointers and returns afterwards. **The callback marker is stronger than a requested/failed EBS, but weaker than EBS return or kernel entry.**

Paths below are relative to `D:/Edk2-For-Xiaomi14T/mu/` unless explicitly otherwise.

## Defects identified and important limitations

### 1. Physical framebuffer presentation does not satisfy the advertised interface

**Proven source mismatch; hardware effect after bootmgfw is not captured.**

`Platforms/Xiaomi/degasPkg/Drivers/DegasGopDxe/DegasGop.c:265-279` advertises a physical framebuffer and BGRX. However `:116-126` repairs byte 3 to alpha `0xff` and calls the command-mode DSI refresh **only from GOP Blt**. Direct stores through FrameBufferBase bypass both operations. The source explicitly states that inherited OVL consumes the reserved byte as alpha. Thus a normal BGRX writer can store a reserved byte of zero, and direct framebuffer drawing has no firmware refresh path. The visible CONSOLE TEST proves the Blt route, not the direct route.

This is the highest-value reason to avoid diagnosing an unchanged starting message as a loader deadlock. An error screen or later boot graphics may exist only in memory. It also applies after EBS, when Boot Services GOP Blt can no longer be used.

The driver truthfully reports width 1220 and pitch 1248 (`degas.dsc:28-31`, `DegasGop.c:271-274`). Microsoft's documented Windows GOP profile asks for equal stride and visible width and continued scanout after EBS; PixelBltOnly is unsupported. Do **not** merely change the reported pitch to 1220: the retained hardware pitch is 4992 bytes and that would corrupt row addressing. A real solution needs validated display configuration/scanout behavior and alpha semantics, or a Windows display driver.

Separate smaller contract defect: `DegasGop.c:53-57` returns success for SetMode(0) without clearing the visible surface. UEFI defines SetMode as setting mode and clearing it to black. This can retain old text; it is not proof of a Windows stop.

**Prepared-source status after V11:** the display audit has now corrected SetMode to clear through Blt, changed QueryMode to copy the complete mode-information structure, and added guarded hardware constant blending plus a cached final-frame flush protocol. These source changes are not hardware validation. The older source references above describe the defect found at initial audit; the post-EBS continuous-refresh limitation remains.

Sources: [Microsoft Windows SoC UEFI requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/uefi-requirements-that-apply-to-all-windows-platforms), [UEFI GOP contract](https://uefi.org/specs/UEFI/2.10/12_Protocols_Console_Support.html).

### 2. SMBIOS is absent from the actual degas firmware volume

**Proven packaging omission; boot-blocking impact unknown.**

`Platforms/Xiaomi/degasPkg/degas.fdf:86-116` contains neither SmbiosDxe nor SmBiosTableDxe. Common `Silicon/Silicium/SiliciumPkg/SiliciumPkg.dsc.inc:489-490` compiles those components, which is not inclusion in this firmware volume. The fresh V8 dispatched-driver list likewise has no SMBIOS publisher. In contrast `Platforms/Xiaomi/lancelotPkg/lancelot.fdf:88-90` includes both.

Microsoft's SoC UEFI guidance requires an SMBIOS entry in the system configuration table, at specification revision 2.4 or newer. That is a real compliance gap, not evidence that bootmgfw immediately requires it in this exact build. Adding just the generic modules without truthful degas identity/memory data would also be incomplete; the common defaults currently produce the observed `Not Specified` platform banner.

Source: [Microsoft Windows SoC UEFI requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/uefi-requirements-that-apply-to-all-windows-platforms).

### 3. Dummy RTC returns an invalid EFI date on a normal fresh boot

**Proven implementation defect, shared with the donor configuration; causal impact unknown.**

`Silicon/Silicium/SiliciumPkg/SiliciumPkg.dsc.inc:328` selects RealTimeClockLibNull. Its `RealTimeClockNull.c:50-59` sets year 2019/month 1 and `Day = ElapsedSeconds / 86400`; before one elapsed day it returns Day=0, then reports EFI_SUCCESS (`:80`). `LibSetTime` returns EFI_UNSUPPORTED (`:85-88`). The runtime wrapper directly returns LibGetTime's output (`Mu_Basecore/EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClock.c:54-70`), so it does not validate this result.

UEFI specifies Day 1..31. A valid deterministic stub date would remove the malformed-data defect but would still not provide a real RTC. This can be reproduced by a host mock counter below one day, without phone access. Do not represent the inherited donor behavior as proof it is safe for Windows.

Sources: [UEFI time-service definition](https://uefi.org/specs/UEFI/2.11/08_Services_Runtime_Services.html), [Microsoft firmware runtime guidance](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/oem-uefi).

### 4. Exposed RAM is intentionally small, with post-WIM headroom unproven

**Proven limited map; OOM is a hypothesis.**

`Platforms/Xiaomi/degasPkg/Library/MemoryMapLib/MemoryMapLib.c:11-17` publishes the approximately 64 MiB low firmware arena and one 1 GiB high window. `Include/RamProbeRange.h:4-5` fixes the added window at `[0x140000000,0x180000000)`. Current source includes no RAM expansion driver in the degas FDF. The prior allocator-test log/handoff establishes 1,133,924,352 conventional bytes at that earlier checkpoint, including the 1 GiB window. The 256 MiB full pattern test validates that allocation, not every byte of the entire 1 GiB.

The 647,695,987-byte WIM can fit contiguously inside the 1 GiB window. That fact alone says nothing definitive about remaining loader, decompression, page-table, kernel, driver, and WinPE working memory. Microsoft explicitly requires room for the whole WIM in contiguous RAM for RAM-disk boot and more than the base 512 MiB when adding components. A simplistic WIM-size versus total-RAM comparison cannot prove OOM.

Lancelot's FDF includes RamManagerDxe (`:85-86`) backed by its MT6768 EMI implementation. Its register-derived range discovery is not safe to transplant to MT6897. Current degas conservative carveouts are preferable to publishing unvalidated memory. If fresh diagnostics establish allocation failure, validate and add another evidence-backed RAM range, or test a smaller known-pristine PE image; avoid declaring all 12 GiB free from Android's memory inventory.

Source: [Microsoft WinPE hardware/RAM-disk requirements](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/winpe-intro?view=windows-10).

### 5. Diagnostic ACPI is coherent but not a complete Windows device platform

**Confirmed implementation boundary, not a discovered wrong GIC base.**

`tools/generate_degas_acpi.py:42-75` generates hardware-reduced FADT/SMC PSCI, one enabled affinity-0 GICC plus GICD, and one CPU namespace object. `tools/prepare_degas_windows_facts.py:183-197` generates GTDT from captured PPIs. Runtime `DegasAcpiDxe.c:87-96` checks boot affinity, first redistributor identity, 13 MHz counter frequency and PSCI version. Published tables/checksums were confirmed in V8. Existing ACPI tests passed during this audit (`python -B diagnostic/tests/test_degas_acpi.py`, 3 tests).

There is no invented storage/USB/display/peripheral namespace, DBG2 transport, secondary-CPU support, or PM power-state implementation in this set. Those omissions limit Windows functionality and debugging. Microsoft's guidance specifically describes DBG2 as the mechanism for exposing a real debug transport. A ramoops SerialPortLib is not itself a Windows KD transport. Missing Windows UFS support is a separate later problem; a genuine RAM-disk WinPE can run without a storage device, so it should not be declared the cause of this pre-EBS read.

Do not add a dummy DBG2 table, lancelot GIC addresses, guessed secondary redistributor bases, CSRT/HAL extension, or MediaTek peripheral resources merely to enlarge the table set. There is no new evidence in V8 that the existing single-CPU ACPI must be replaced to advance the loader.

Source: [Microsoft ACPI system description tables](https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-system-description-tables).

## BDS, protocols and handoff findings that are already addressed

- `BdsLifecycle.h:5-17` signals EndOfDxe and installs ReadyToLock before external image execution. `WindowsBoot.h:80-89` uses the standard EfiBootManagerBoot path. No missing generic BDS architectural driver should be added on top of the custom BDS protocol.
- Actual FDF now includes DevicePath/Print/HII/GraphicsConsole/ConSplitter/SetupBrowser/DiskIo/Partition/FAT/security/runtime/variable/capsule/reset/RTC/timer/GIC modules. V3's FormBrowser2 ASSERT is historical; SetupBrowser is now present (`degas.fdf:107`).
- `UfsBlockIo.h:24-69` performs synchronous bounded chunks at TPL_NOTIFY, validates response/error/residual bytes and copies only completed data. Timer callbacks cannot run during a large read at this TPL, but the inner loop explicitly feeds the watchdog. Their absence there is not by itself a new deadlock.
- The V8 byte-copy bottleneck is already changed in current `UfsCopy.h:3-17` to aligned scalar 64-bit source reads. V9 hardware speed/correctness still lacks a fresh log; do not describe the old byte loop as current source.
- `OsHandoff.h:7-18` is idempotent and stops HCI interrupt/list activity with bounded HCE polling. Shared TimerDxe disables its timer (`TimerDxe.c:97-103`); GicV3 exits through its generic interrupt/interface/distributor shutdown (`ArmGicV3Dxe.c:722-748`). OP12-specific distributor logic is preprocessor gated.
- Current `Checkpoint.c:210` enables the V10 diagnostic branch: `OsHandoff.h:23-26` arms a reset-only 10-second watchdog after the EBS event. This intentionally resets even if Windows starts. The normal branch disables the watchdog (`:28`). Neither branch proves hardware EBS success without a fresh result.
- Shared PSCI ResetSystemLib is used, not a null reset implementation (`SiliciumPkg.dsc.inc:212-213`). Actual OS reset/shutdown operation remains unvalidated; no reset experiment was performed here.

## Highest-value offline and next diagnostic checks

1. Preserve the V10 image/source hashes and compare its actual included modules to the FDF, rather than inferring inclusion from build success. Establish no unexpected functional changes relative to V9 beyond the documented EBS recovery branch.
2. Keep one short, fresh, automatically preserved log as the next hardware discriminant when the user authorizes hardware work. Record complete/returned large reads, any allocation/image error, EBS marker and diagnostic watchdog marker. Avoid a manual-reset-only result, which already lost V7/V9 evidence.
3. Interpret the result by boundary: continued progress without completed read = firmware I/O path; no I/O plus no EBS = loader/allocation/error screen; EBS callback marker = successful map-key validation reached, then investigate remaining EBS callbacks/Windows handoff. If this last case occurs, a final RAM-only breadcrumb immediately before CoreExitBootServices returns is more diagnostic than broad ACPI edits.
4. For visibility, an explicit pre-EBS direct-framebuffer test can isolate alpha and command-mode refresh from GOP Blt in a future authorized diagnostic. It needs controlled pixels, separate refresh/no-refresh observations, and bounded automatic recovery. Do not use a periodic Boot Services refresh timer as a claimed post-EBS solution.
5. Log a compact memory-map/protocol/config-table inventory before bootmgfw: actual conventional totals/largest interval, ACPI and SMBIOS GUIDs, GOP format/stride, GetTime status and returned date. This would make RAM/SMBIOS/RTC state concrete without relying on module assumptions. It does not require exposing all DRAM.

No production fix or hardware validation was performed by this audit. Genuine current defects, plausible next-stage risks, and historical fixes are deliberately separated above.

## Follow-up before V11: actual bootmgfw → winload → EBS chain

Requested deeper follow-up while the other audits investigate display. Still no phone access or production edits.

### BCD and WIM inspected, without mounting or modifying either

Read-only WSL Hivex inspection of both `artifacts/oneplus12/windows-boot-inputs/EFI/Microsoft/Boot/BCD` and `.../esp-staging/EFI/Microsoft/Boot/bcd` found the same SHA-256, `b90ef16b94c3dda7d76cc39840befaa9786887517445c3b6f47ebc806d0ab105`, and identical semantic trees. These are the pristine source/staging stores referenced by the degas preparation, not a newly fetched live ESP store.

The effective default is `{7619dcc9-fafe-11d9-b411-000476eba25f}` (Windows Setup). Its device and osdevice elements reference a RAM disk with `\sources\boot.wim`; the path element is `\windows\system32\boot\winload.efi`, systemroot is `\windows`, and winpe/detecthal are enabled. The referenced RAM-disk options object is `{7619dcc8-fafe-11d9-b411-000476eba25f}`, with SDI path `\boot\boot.sdi` and a boot-device parent encoding. The store contains a 30-second boot-manager timeout. No degas-specific wrong fixed partition GUID or OP12-specific custom loader path was found in these stores. This is structurally the expected WinPE RAM-disk configuration. [Microsoft RAM-disk BCD configuration](https://learn.microsoft.com/en-us/windows/deployment/configure-a-pxe-server-to-load-windows-pe).

Independent direct reading of the WIM header and uncompressed XML resource, without extracting the image, found two ARM64 WindowsPE images, both build `10.0.26100.8037`, and **boot index 2**, Microsoft Windows Setup. Index 1 is Microsoft Windows PE. The file is 647,695,987 bytes. Reported expanded-file totals of approximately 2.61/2.73 GB are disk/file totals, **not** proof that all those bytes must be simultaneously resident at boot.

The V8 request arithmetic is compatible with reading this WIM: rounding its size to a 4096-byte block gives **647,696,384** bytes. The last cumulative read count plus request remainder is 658,927,616; subtracting 647,696,384 leaves 11,231,232 earlier-read bytes. On that interpretation, the in-flight WIM-sized request has completed 392,015,872 bytes and has 255,680,512 left. This is a useful consistency check; the log still does not name the file.

### EBS event ordering, including the point where the watchdog is disabled

The actual shared event implementation matters here:

1. `Mu_Basecore/MdeModulePkg/Core/Dxe/Event/Event.c:508` inserts new signal-group events at the **head** of the registration list.
2. `:247-266` traverses that list from the head and queues matching callbacks.
3. `:236` queues notifications at the **tail** of their TPL queue; `:158-195` dispatches each TPL queue from the head.

Therefore, for these equally prioritized TPL_NOTIFY callbacks, the Windows EBS event registered immediately before launching bootmgfw runs before the Timer/GIC events registered during driver initialization. The fresh V8 driver-load order has GIC before Timer. Among these three, successful EBS dispatch is consequently **Windows handoff → Timer disable callback → GIC disable callback**. Other earlier registered callbacks, including Capsule's lock callback, still exist. This is source-derived ordering, not captured V9 execution.

CoreExitBootServices first disables the timer (`DxeMain.c:809`), then validates the key and runtime descriptor alignment (`Mem/Page.c:2302-2358`). A failed key/alignment check returns EFI_INVALID_PARAMETER without calling WindowsExitBoot. On success the Windows callback prints its marker, stops UFS, then normal V9 disables the hardware watchdog. Only afterwards does the event list finish and CoreExitBootServices report status, process memory protections, disable CPU IRQs, clear Boot Services, set `AtRuntime`, and return (`DxeMain.c:828-877`). Thus **V9 can disable its recovery watchdog and then hang inside a later EBS callback without ever returning to winload**. This possibility must remain separate from a Windows kernel hang.

The generic GIC callback (`ArmGicV3Dxe.c:722-748`) writes the interrupt disable registers, disables ICC_IGRPEN1_EL1, and writes zero to GICD_CTLR using `ArmGicCommonDxe.c:230-236`. It has no deliberate wait loop. Its CPU interface write is in `Common/Mu/MsCorePkg/Library/MuArmGicExLib/GicV3/AArch64/ArmGicV3.S:69-72`. The Timer callback only disables the architected timer (`TimerDxe.c:97-103`). There is no evidence of an unbounded software poll in either callback. A bus/privilege trap remains possible until captured; blindly copying the OP12 distributor workaround is not justified by the current evidence.

### UFS shutdown does not normally race asynchronous firmware I/O

`UfsBlockIo.h` publishes synchronous EFI_BLOCK_IO_PROTOCOL only. It does not publish BlockIo2/DiskIo2 or queue multiple requests. Each `UfsProbePacket` (`UfsCapacity.h:4-39`) rejects a preexisting active doorbell, submits one slot, waits for doorbell clear or a bounded failure, checks OCS, issues UTRL stop, and waits for that stop before returning. `UfsBioRead` copies the payload only after this completion and returns before the loader can proceed on the single boot CPU.

Accordingly, after a successful synchronous read there is no legitimate firmware request still pending when winload reaches EBS. DegasOsHandoff sets `mUfsActive=FALSE` and prevents future callbacks from touching a stopped HCI. Keeping firmware UFS operating after EBS would not supply a Windows storage driver. The reported pre-stop UTRL/UTMRL doorbells and post-stop HCE are still valuable diagnostics, but a pending-I/O race was **not** found in this source path.

A genuine HCI stop failure is only logged, not propagated out of the void EBS callback (`OsHandoff.h:15-18,31-32`); the normal branch would still disable the watchdog. This is a concrete robustness gap. It does not establish that V9 actually hit that failure, nor that merely leaving HCE enabled would prevent RAM-disk Windows from starting. For diagnostics the important distinction is an explicit HCE-stop-success/failure record before any normal watchdog disable.

### MMU, runtime protection and linked build configuration

`SiliciumPkg/Library/MemoryInitPeiLib/MemoryInitPei.c:163-168` identity maps every published region and `:175` calls the standard ArmConfigureMmu. `Mu_Basecore/UefiCpuPkg/Library/ArmMmuLib/AArch64/ArmMmuLibCore.c:700-755` derives the address width and TCR physical-size field from architectural capability, not a hard-coded 32-bit limit. No cast that truncates the `[0x140000000,0x180000000)` RAM window was found in this path. The existing successful high-window allocation test is additional evidence against a simple pre-EBS 32-bit RAM-address truncation.

No DXE memory-protection-settings HOB producer appears in the degas SEC/MemoryPeim path. Its selected PlatformPeiLibNull produces only an FV HOB. `MdeModulePkg/Library/MemoryProtectionHobLib/DxeMemoryProtectionHobLib.c` consequently zeros `gDxeMps` when that HOB is missing; `MemoryProtection.c:1210` skips its EBS runtime-unprotection loop when ImageProtectionPolicy is zero. A protection-policy-induced transition loop is therefore not supported by this configuration. This inference should still be confirmed by a compact observed policy/HOB dump if that boundary is reached.

The generated GIC and RuntimeDxe GNUmakefiles contain `-march=armv8-a -mstrict-align` and do not define OP12_WINDOWS_BOOT or ONEPLUS12_SEC_PROBE. The extra RuntimeDxe Op12Trace calls compile to no-ops (`Core/RuntimeDxe/Op12Trace.h:35-39`). They do not dereference OP12 physical addresses on degas.

The linked DxeReportStatusCodeLib AutoGen.h sets `PcdReportStatusCodePropertyMask=0x00`. Consequently the generic core REPORT_STATUS_CODE call is not a reliable expected serial milestone in this build. Missing `PROGRESS CODE` output must not be treated as evidence that the core stopped at that line; use an explicit bounded breadcrumb.

There is a real inherited runtime alignment concern: common `SiliciumPkg.dsc.inc:13` defines `__DEPRECATED_AARCH64_4K_RUNTIME_GRANULARITY`; `MdePkg/Include/AArch64/ProcessorBind.h:165-170` then permits 4 KiB runtime allocation granularity. Runtime PE sections are separately linked at 64 KiB alignment (`SiliciumPkg.dsc.inc:22-23`). CoreTerminateMemoryMap validates only the selected allocation granularity. Modern AArch64 UEFI specifies 64 KiB-aligned/multiple runtime descriptors, so a captured runtime map should check base and size for every EFI_MEMORY_RUNTIME entry and ACPIMemoryNVS/Reclaim interval. This is a specification risk, **not proof that the current Windows build rejects this map**; lancelot shares the common policy. [UEFI AArch64 requirements](https://uefi.org/specs/UEFI/2.10/02_Overview.html), [UEFI 2.11 specification](https://uefi.org/sites/default/files/resources/UEFI_Spec_Final_2.11.pdf).

The ramoops range is EfiReservedMemoryType, not an EFI_MEMORY_RUNTIME range. The RAM serial writer hard-codes its physical address and has no virtual-address conversion. That is suitable for the existing pre-EBS/EBS physical-context breadcrumbs. It must not be assumed safe for arbitrary debug prints after Windows switches mappings. A later SetVirtualAddressMap trace needs a separately designed runtime-backed buffer or bounded tracing before the mapping switch; do not extend the existing physical writer into virtual runtime on assumption.

### What V9's long wait does and does not favor

The only degas path found that clears the actual MT6897 watchdog EN bit is the normal OsHandoff branch, after its UFS-stop attempt. Generic UEFI WatchdogTimerDxe's SetTimerPeriod simply calls EFI SetTimer (`WatchdogTimer.c:151-165`): a loader's SetWatchdogTimer(0) cancels a software timer and does **not** directly disable the MT6897 hardware timer. A failed MapKey or hang while servicing a synchronous read should leave the hardware watchdog enabled. Even a bootmgfw error return ultimately returns through the menu's deliberate 10-second reset path.

Given V8's verified recovery, V9's faster-copy change and absence of reset make reaching the normal EBS watchdog-disable branch the leading **inference**. It is stronger than a random possibility, but still cannot distinguish a later firmware EBS hang, winload work, kernel entry, a Windows-controlled reset/clock transition, or independent watchdog/recovery malfunction. The lost log prevents upgrading that inference to fact.

**Superseded for the current V11 run:** the fresh final core breadcrumb now rules out the previously proposed stall inside the instrumented EBS body up to its return instruction. This historical V9 uncertainty must not be presented as the current observed failure boundary.

### Precise next correction/diagnostic test

No new unconditional fatal handoff defect was established. The focused next test should retain bounded hardware recovery and add stage evidence instead of changing BCD/ACPI/GIC simultaneously:

- Before the boot, capture actual runtime descriptor base/length/alignment and the current memory-protection settings.
- In a diagnostic build, add RAM-only begin/end breadcrumbs around successful EBS event dispatch, and a final breadcrumb immediately before CoreExitBootServices returns; if needed, add before/after marks to GIC shutdown. Do not draw or allocate at that boundary. Preserve the short hardware watchdog through all of it.
- If the final core-return breadcrumb is absent but Windows handoff marker is present, isolate the intervening firmware callback. If present, proceed to a carefully scoped winload/kernel-transition observation; do not label that marker itself kernel entry. If no EBS is reached, focus on complete WIM read, allocations and hidden loader error rather than OS UFS driver work.
- Separately correct the proven RTC/SMBIOS/display shortcomings, but do not conflate those compliance fixes with a demonstrated repair of this handoff.

## Post-V11 review of the prepared final-frame EBS integration

Read-only review of `Drivers/CheckpointDxe/WindowsBoot.h`, `Include/Protocol/DegasDisplayRefresh.h`, and the called GOP flush helper found **no EBS-contract blocker** in the integration visible at review time:

- WindowsBoot caches the board-specific protocol before StartImage. It does not locate a protocol during EBS.
- WindowsExitBoot first records the framebuffer observation, disables further observation, and calls DegasOsHandoff. This arms the diagnostic reset before the display call. It then invokes only the cached Flush pointer and logs its result.
- The Flush method checks its own interface identity and requires the already verified opaque setup. It does not attempt OVL setup/rollback at EBS. Its readiness checks read mapped registers; the existing refresh path performs only cache cleaning, bounded counter-based delays, and display MMIO writes.
- No normal GOP Blt, Boot Services call, allocation/free, protocol installation, memory-map change, or framebuffer pixel store occurs in that call path. The protocol object and driver code remain valid during this EBS notification; they are not being offered as an OS runtime interface.
- Each display-idle wait is capped at approximately 100 ms. The refresh path contains up to three such waits, so the aggregate polling bound is approximately 300 ms, plus cache/MMIO work. Do not describe the complete Flush as a single 100 ms bound.
- Logging uses the corrected RAM ring writer's save-disable/restore IRQ guard. It restores the entry interrupt state; the flush does not persistently change CPU IRQ routing/state or the GIC configuration.

A final-frame snapshot shows what the loader rendered. Continuous post-EBS refresh still requires a hardware display loop or an OS driver.

V11 independently observed `smbios2=0 smbios3=0`, invalid successful GetTime date `2019-1-0`, 1,125,634,048 conventional bytes and a 1 GiB largest free interval (`console-ramoops-0:498-499`). Those initial audit gaps are now observations rather than packaging inferences, but V11's progress shows they did not stop this run from reaching the final EBS breadcrumb.

Raw physical ramoops access at virtual runtime is a mapping risk: generic, non-OP12-gated runtime paths include `ArmPsciResetSystemLib.c:88` (ResetWarm fallback DEBUG_INFO), `Variable.c:2488` (out-of-space DEBUG_WARN), and `Variable.c:358` (RecordVarErrorFlag DEBUG_ERROR). The serial ring remains reserved physical memory and has no SetVirtualAddressMap conversion. The IRQ reentrancy guard does not solve that mapping issue. No evidence links this risk to the fresh V11 stop; this final-frame EBS call still runs in the firmware's physical mapping context.

## Focused follow-up: EBS thumbnail consumer

Read-only review of the subsequently added FrameThumbnail call found its BGRX-to-RGB565 conversion correct: R comes from source bits 23:19, G from 15:10, B from 7:3. Sampling maps the inclusive endpoints to X=0..1219, Y=0..2711 with 1248-pixel stride; the final 4-byte load stays inside the reserved framebuffer surface. Each row is 20 prefix bytes + 488 hex digits + newline = 509 bytes. All 272 rows are 138,448 bytes, with short header/trailer records keeping the result below 140 KiB. Added to the 33,669-byte V11-style log, this fits the 262,132-byte ring payload with substantial margin. The 520-byte local line buffer is adequate; actual function stack also includes scalar locals/ABI overhead.

FrameThumbnail only reads the fixed mapped framebuffer and writes bounded ASCII to reserved RAM. Its position after the diagnostic watchdog is armed and before cached Flush adds no Boot Services, allocation, map-key mutation or framebuffer stores. It snapshots RGB independently of alpha and should help identify hidden Windows content even if the OVL guard rejects the final transfer.

## Focused follow-up: smallest MT6897 GCE fetch smoke packet

No hardware accesses or implementation edits were made. Reviewed `diagnostic/include/degas_gce_one_frame.h` against source commit `3c4ccd07d5624879555ec710c11bb8818e5cb5c6` in `evidence/sources/mt6897-device-modules` and the extracted installed `evidence/extracted/2026-10-26-gce/mtk-cmdq-drv-ext.ko` (SHA-256 `ff73e817d3c2fc2fdc11b3bddb3bbb6b8bf6a962cb4f55f94349608d6065a3ba`). Source line references below are to `drivers/misc/mediatek/cmdq/mailbox/mtk-cmdq-{helper,mailbox}-ext.c`, also copied as helper.c/mailbox.c in the temporary audit directory.

For sentinel `0x12345678`, the minimal normal finalized packet is three 64-bit words:

| Operation | Golden word |
| --- | --- |
| LOGIC ASSIGN thread SPR0, sentinel | `0xa080000012345678` |
| EOC, increment execution count, IRQ disabled | `0x4000000000000000` |
| Relative JUMP_PASS +8 bytes | `0x1000000000000001` |

Helper `:1905-1911` encodes ASSIGN with A-type=register, index=0, immediate B/C and LOGIC opcode. `:2480-2487` sets EOC s_op=0 when cnt_inc=true and low bit=0 when no_irq=true. `:1936-1942` shifts relative jump byte offset by gce_shift_bit. Installed-code corroboration: `.text:e398` ORs ASSIGN prefix `0xa080000000000000`; `.text:10ae0-10af8` selects the stated EOC word; `.text:f6cc-f6d0` performs offset>>shift and ORs the relative JUMP opcode. SPR0 is thread-local; this packet has no GPR/global mask use and no display writes.

**END is the trailing JUMP's address for an ordinary one-shot finalized packet.** Mailbox `:891-897` computes one-past used command data, then `:1087-1088` subtracts one instruction for !loop before writing END. Installed `.text:9058-9080` explicitly chooses -8 for !loop and adds it before the encoded END write. Thus a three-word packet uses PC=`encode(dma)` and END=`encode(dma+16)`, not encode(dma+24). The existing one-frame prototype's `(count-1)*8` is correct for this convention. Reaching END need not establish execution of the trailing pass JUMP; EOC precedes it. The special cmdq_init_cmds stream uses a different one-past convention and should not override normal packet submission.

Address encoding is `(dma + 0x40000000) >> 3`: cmdq.h `:117`, mailbox `:2944-2948,2973` select shift=3/mminfra=BIT(30) for MT6897. The argument is the verified controller DMA/IOVA address, **not automatically the CPU physical address**. The source macro and installed getter `.text:df4-df8` corroborate shift/aperture arithmetic; neither establishes the correct new diagnostic mapping by itself.

For smoke-test completion, read a counter baseline at thread+0x28 **after warm reset and before enable**, and ensure initial SPR0 is not already the sentinel. Require the new sentinel, PC==encoded END, and no IRQ error bits 0x12; additionally require execution-count progress (one EOC should advance once). Record before/after counter values. Source treats the counter as u32 and uses `cookie != previous` for liveness (`mailbox.c:1703-1713`); it does not establish an undocumented 16-bit wrap mask. A noIRQ EOC does not require IRQ_DONE bit0. PC equality alone is a weaker fetch/execution observation than these combined predicates.

Cleanup: suspend via thread+8=1; source `:641-650` treats an already-disabled thread as successfully suspended, otherwise waits for status bit1. Capture SPR/counter/PC/IRQ **before** reset. Warm-reset the owned thread, verify reset bit0 clears, then disable task at thread+4=0. Source `:676-690,741-742` and installed `.text:4738-4748` confirm reset-before-disable. The kernel writes global slotcycles=0x3200 after reset; this is a separate shared register operation, not a global controller reset. Retain/quarantine command storage if stop/reset cannot be established. The current prototype's unconditional suspended-bit requirement can falsely report failure when the task is already disabled. IRQ status acknowledgment is `~irq_flag` (W0C), source `:1335`; do not substitute a W1C convention.
