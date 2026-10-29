# Narrow experiment: single CPU7 as Windows boot processor

The user requested a usable single performance core while SMP remains broken.
This does not resume all-core investigation or frequency programming tonight.
Android sysfs confirms policy0 CPUs0..3 max2200000, policy4 CPUs4..6 max3200000,
policy7 CPU7 max3350000. CPU7 is Cortex-A715 with MPIDR affinity700.

Opt-in composite packaging `--boot-core 7` patches only the Android route's
normal-UEFI branch to a separate early blob at40011000. BOOT ANDROID still
bypasses that blob and starts stock Linux on fresh LK's CPU0.

The early blob saves LK arguments/SP/DAIF, starts CPU7 without relying on the
known-broken PSCI context argument, waits for its guarded ready publication,
then CPU0 calls CPU_OFF. CPU7 proceeds only after AFFINITY_INFO confirms CPU0
OFF. Any ambiguous ownership quarantines both paths with a bounded watchdog;
it must never let two CPUs execute the shared SEC stack.

UEFI build option DEGAS_BOOT_CORE=7 requires current affinity700; only GICC UID7
is enabled, with its discovered redistributor. Other GICCs are disabled and all
Parking fields zero. DSDT C007/_UID7 remains matched. Generic GICv3/timer code
uses current MPIDR; SEC/platform initializer has no CPU0 gate.

Normal builds default to core0. Prime mode uses Parking=0. A 30-second EBS
watchdog bounds the first Windows test. Initial hardware check is UEFI menu
BOOT CPU7 and ACPI CPUS1/8 before selecting Windows. No PLL/DVFS writes occur;
advertised3.35GHz is not a promise of the actual Windows operating frequency.

Tests: actual handoff instructions in a two-CPU emulator (17 scenarios), and
actual PrepareCpuMadt source under ASan/UBSan for core0/core7, all affinity
mismatches, GIC/PSCI failures, permuted redistributor discovery and DSDT UID.
Firmware handoff/GIC/power behavior remains hardware-test scope.

First hardware result: user observed BOOT CPU7, MIDR411FD4D0, ACPI CPUS1.
Therefore the early handoff reached the actual A715 and installed single-core
ACPI. Windows startup/performance remains unverified at this checkpoint.
First image SHA492de76d6eb463f570995a030c56a2aa98415f83a88d41a3d83031c9aeb9c730.
Next build keeps BOOT_CORE7/PARKING0 and disables diagnostic EBS reset so full
Windows can complete boot. No frequency control has been added.

ADB capture `evidence/raw/2026-10-28-degas-prime-uefi-result/` confirms all
CPU0..6 AFFINITY_INFO=OFF, CPU7=ON, GICR=C520000, ACPI cpu1/boot_uid7/mpidr700.
BOOT ANDROID also succeeded after this UEFI-only prime run.

Full-Windows test image flashed: artifacts/degas-prime-desktop-v1/boot.img,
SHA256 b67c1a1b897ff13eaa2ee5180291dda70a5afecf77c661ee0b40c5e3e2d92636.
EBS watchdog-off branch verified in linked EFI. Awaiting Windows hardware
result. User's earlier broad SMP pause remains in effect; only this narrow
prime-core request was resumed.

User reports Windows on CPU7 is much faster than the previous A510 boot.
No Task Manager frequency was supplied, so actual operating MHz remains unknown.
Requested follow-up: restore native full-screen GOP; diagnostic inset now
defaults OFF (DEGAS_DIAGNOSTIC_INSET=0), with CPU7 and EBS-watchdog-off retained.

Fullscreen follow-up flashed: artifacts/degas-prime-fullscreen-v1/boot.img,
SHA256 d4297ff9f41b894087ec259ecfa5bf141404bb0ee43fcc950bbb85213291485e.
Native GOP1220x2712/baseFCC2F000/pitch1248 is passed unchanged. Inset Arm/Disarm
are no-ops by default; optional diagnostic source switch can restore inset.
Awaiting visual confirmation. No new low-impact viewport-specific tests added;
firmware build and linked watchdog/native-view branch configuration checked.
