# degas: reusable lancelot boot foundation, 2026-10-25

User requested adapting Redmi 9's working WinPE path. Local `mu/Status.md` lancelot section documents Windows PE only. This comparison uses the checked-out source, not an assumption that MT6769T and MT6897 are register compatible.

## Changes

- Reuse `MdeModulePkg/Library/UefiBootManagerLib` (`EfiBootManagerBoot`) used by lancelot BdsDxe. Replaces our direct LoadImage/StartImage: BootCurrent/temporary Boot option, PreReadyToBoot, ReadyToBoot, standard loader setup/parent handling, return status. NVRAM is emulated (`PcdEmuVariableNvModeEnable=TRUE`), not a new GPT or UFS write path.
- Add the same generic PrintDxe, DevicePathDxe, ConPlatformDxe, ReportStatusCodeRouterRuntimeDxe and StatusCodeHandlerRuntimeDxe to degas FDF. HiiDatabase, GraphicsConsole and ConSplitter were already added for v1.
- Retain minimal on-device BDS selection menu and validated MT6897 UFS/GOP/key drivers. Do not install a second BDS architectural protocol.
- `OsHandoff.h`: successful ExitBootServices notification stops interrupt generation and both UFS lists, disables HCE with bounded polling, then disables watchdog by read/modify/write EN bit with key. Idempotent; no allocation, protocol changes, or Boot Services calls. Existing synchronized UFS reads use fixed reserved DMA.
- Watchdog operation mirrors the purpose of lancelot `MT6768Pkg/Library/PlatformSecLib/PlatformSec.c::DisableWatchDogTimer`, but uses MT6897 address 0x1c00a000 and preserves unrelated mode bits following `evidence/sources/degas-buttons/mtk_wdt.c::mtk_wdt_stop`. It runs at EBS, not early SEC, so pre-OS recovery remains available.
- Existing shared `ArmPkg/Drivers/TimerDxe` disables the timer at EBS; `ArmGicDxe/GicV3` disables interrupt sources/interface/distributor. The OP12-specific branch is preprocessor-gated and not enabled for degas.

## Core and exclusions

`CoreExitBootServices` is already the same shared Mu DxeCore. It validates MapKey before signaling successful EBS, then terminates services and updates system table. No private replacement or pointer hook is introduced.

Lancelot BdsDxe includes KernelErrataPatcherLib, but its effective `ErrataPatchesLib` is the null implementation: EFI_UNSUPPORTED and no kernel-transfer shellcode. There is no Redmi-specific Windows kernel fix here to transplant blindly. Keep that hook out of degas.

Do not copy lancelot GIC addresses, 8-core ACPI, MT6358 PMIC, eMMC/MSDC, memory map, or old OVL offsets. degas retains hardware-verified values. SMBIOS/RamManager integration is not claimed in this change.

## Diagnostics and validation

First WBM attempt log was lost after manual hard reset. Last visible screen alone does NOT identify failing stage. v2 stage-only build was compiled but NOT flashed after user requested foundation changes. New foundation build retains screen markers and 120-second pre-EBS watchdog-feed budget (timer plus BlockIO); after successful EBS watchdog is disabled.

Host test `diagnostic/tests/test_degas_os_handoff.c`: stop order, preserved watchdog mode bits, idempotency PASS. It does not prove physical shutdown, link stability or Windows boot. Full firmware build and hardware test are tracked in handoff notes.
