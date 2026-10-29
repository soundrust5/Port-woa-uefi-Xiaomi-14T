# Degas ACPI / parking memory audit — 2026-10-28

Read-only source audit; no phone operations or source changes.

## Validated ACPI structure

GICC offsets match ACPI: UID +8, flags +12, parking version +16,
mailbox address +24, redistributor +60, MPIDR +68. UID0..7 correspond to
DSDT ACPI0007 devices C000..C007. Boot affinity0 is first. Runtime GICR
bases are matched by GICR_TYPER affinity, not inferred from index.

CPU Interface Number0 is consistent with GICv3 without v2 compatibility.
Parking advertises FADT ARM_BOOT_ARCH=0 and ParkingVersion1; ordinary PSCI
uses ARM_BOOT_ARCH=1 and ParkingVersion0. A510 efficiency0 / A715 efficiency1
is correct: lower ACPI power-efficiency class means more efficient.

No demonstrated UID, field-offset or FADT error explains a NULL scheduler
pointer. Missing PPTT is a topology completeness concern, not a proven cause.

## Exact memory-attribute caveat

Parking.h currently calls Cpu->SetMemoryAttributes directly with EFI_MEMORY_UC.
ArmPkg/Drivers/CpuDxe/AArch64/Mmu.c maps UC to Device plus XN and WC to
Normal noncacheable. CpuMmuCommon.c changes page tables without updating GCD.
CoreGetMemoryMap in MdeModulePkg/Core/Dxe/Mem/Page.c copies Entry->Attribute
from gMemoryMap. In this tree Gcd.c updates that map through
CoreUpdateMemoryAttributes under GCD_SET_CAPABILITIES_MEMORY_OPERATION;
SetMemorySpaceAttributes alone is therefore not proof all three views agree.

Before changing the policy, record actual CPU translation attributes, GCD
attributes/capabilities, and GetMemoryMap descriptor for each mailbox page.
Do not assume a successful CPU setter makes the OS-visible descriptors agree.

This is a concrete bookkeeping concern, not proof that it causes BSOD.
The reported KMODE parameters indicate write access to VA0, not a demonstrated
STATUS_DATATYPE_MISALIGNMENT. Runtime kernel base and faulting CPU remain
unverified, so the SharedReadyQueue interpretation is still conditional.

## Follow-up after LocalID-correct v3

First-AP green with six white means only one AP reached the firmware branch
marker; it does not prove which Windows phase failed. M/C/I=0 at that branch
rules out an already-enabled AP data cache in the parking stub itself.

Do not issue arbitrary implementation-defined CPUECTLR/SMPEN accesses from
EL1: they may trap, and A510/A715 definitions differ. Same-firmware PSCI CPU_ON
normally establishes coherent CPU participation independently of its caller;
there is not yet evidence of an SMPEN difference between Linux and UEFI.

Safe useful instrumentation: snapshot mailbox OS section0..2047 read-only into
a separately reserved diagnostic region before the branch, preserving x0/x1
and all OS bytes. Also record architectural SCTLR/MAIR/TCR/TTBR0/TTBR1,
MPIDR and CurrentEL. Do not change these registers. Compare the mailbox
contents and CPU-memory-map views against the Windows HAL handoff code.
Capturing target-entry bytes may be valuable only after bounds/identity mapping
are checked; blindly dereferencing a supplied address can fault.

## Primary references

- https://uefi.org/specs/ACPI/6.5/05_ACPI_Software_Programming_Model.html
- https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/uefi-requirements-that-apply-to-all-windows-platforms

Microsoft requires PSCI where supported. This parking path remains a diagnostic
fallback for the observed CPU_ON context-transfer problem, not a production
power-management implementation.

### Coherency source check

Arm TF-A's cortex_a510.S (lines13..16) and cortex_a715.S (lines16..19)
explicitly require HW_ASSISTED_COHERENCY and describe coherency as hardware
handled. This is evidence against porting the old A53/A57-style
CPUECTLR.SMPEN bit6 recipe to these cores. It does not exclude a platform
interconnect/SMMU configuration bug, but no such bug is currently observed.

Primary source:
- https://raw.githubusercontent.com/ARM-software/arm-trusted-firmware/master/lib/cpus/aarch64/cortex_a510.S
- https://raw.githubusercontent.com/ARM-software/arm-trusted-firmware/master/lib/cpus/aarch64/cortex_a715.S
