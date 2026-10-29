# PSCI context-shim research — 2026-10-29

Bounded read-only local/source research; this report is the only new artifact.
No phone operations or Windows/firmware changes. Starting evidence is the v2
monitor's confirmed Start.c:1386, problem0xD/status0xC0000001 and matching
runtime NT identity, rather than the earlier ambiguous screen reading.

## Concrete Redmi 9 finding: its checked-in configuration is single-core

The local Mu lancelot DSC selects `lancelot/AcpiTables.inf`. That INF explicitly
selects `MT6768/APIC.UniCore.aml`. Its checked-in disassembly enables CPU0 and
disables CPU1..7, with no parking addresses. Thus PcdCoreCount=8 in the SoC
DSC is not evidence of working Windows SMP. The corresponding MediaTek
PlatformHelper only returns at EL1 or drops EL2 to EL1. No context-repair
SMC interceptor or parking implementation was found in the inspected local
MediaTek/lancelot source. Current upstream project sources agree.

Primary project files:

- [lancelot ACPI selection](https://raw.githubusercontent.com/Project-Silicium/Silicium-ACPI/main/Platforms/Xiaomi/lancelot/AcpiTables.inf)
- [selected uniprocessor MADT disassembly](https://raw.githubusercontent.com/Project-Silicium/Silicium-ACPI/main/Silicon/MediaTek/MT6768/Decompiled/APIC.UniCore.dsl)
- [MediaTek platform entry](https://raw.githubusercontent.com/Project-Silicium/Mu-Silicium/main/Silicon/MediaTek/MT6768Pkg/Library/PlatformSecLib/AArch64/PlatformHelper.S)

## What an actual context-preserving firmware route requires

Arm's PSCI contract requires preserving the caller's opaque context for target
CPU entry. TF-A's generic `psci_get_ns_ep_info()` clears its entry arguments
and explicitly assigns `ep->args.arg0 = context_id`; the warmboot path installs
that saved entry context before exiting EL3. This identifies the proper
architectural repair layer if the defect is inside the resident PSCI provider.
It is not proof that this phone's vendor BL31 has the identical implementation,
or that a usable/rebuildable MT6897 BL31 replacement exists.

Primary sources: [Arm PSCI specification](https://documentation-service.arm.com/static/6703a8b8d7e4b739d817e10d),
[TF-A PSCI context construction and warmboot](https://raw.githubusercontent.com/ARM-software/arm-trusted-firmware/master/lib/psci/psci_common.c).

A concrete upstream mediation pattern exists in Linux KVM's nVHE PSCI relay:
`psci_cpu_on()` saves guest entry PC and X3 per target MPIDR, substitutes a
hypervisor entry, and `__kvm_host_psci_cpu_on_entry()` reinstates X0 and the
EL1 return PC. This is EL2 software, not an EFI protocol. It also passes its
own init-context pointer through lower PSCI, so it is not a drop-in workaround
for broken lower-firmware context delivery: adaptation would need an independent
per-MPIDR bootstrap record, as our existing probes already use.

Primary implementation: [Linux nVHE PSCI relay](https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/kvm/hyp/nvhe/psci-relay.c).

With the observed BSP/AP entry at EL1, current UEFI cannot install that EL2
interceptor or change the arguments/return value of Windows's native SMC.
An EL2 shim requires a boot-chain handoff that actually grants EL2 ownership
on BSP and APs; no such supported route was established here. Merely setting
FADT's HVC bit does not create a hypervisor. Prestarting APs while retaining
native PSCI also leaves CPU_ON reporting an already-running core rather than
providing Windows with repaired CPU_ON semantics.

## Result and next useful distinction

No concrete existing MediaTek/Redmi9 Windows SMP context-loss workaround was
found in this bounded primary-source check. There is no demonstrated supported
EL1-only firmware shim for the current native Windows PSCI path. The credible
architectural routes are repairing the actual PSCI provider or obtaining an
earlier EL2 handoff and implementing PSCI mediation; neither is presently a
ready-to-build Degas solution.

Microsoft explicitly requires PSCI where available and excludes the legacy
parking protocol on such platforms. The current parking experiment therefore
does not establish a supported replacement for PSCI. Native Linux's arm64 CPU
startup uses a shared secondary entry and its CPU-on interface has no caller
context argument, so successful Android SMP does not validate Windows's use
of nonzero PSCI context.

Primary references: [Microsoft multiprocessor startup requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/uefi-requirements-that-apply-to-all-windows-platforms#uefi-arm-multiprocessor-startup-requirements),
[Linux arm64 PSCI startup](https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/kernel/psci.c).

The immediate useful work remains distinguishing exactly why this Windows
start method returns C0000001 after parking release, without replacing a NULL
callback or scheduler pointer. Any proposed context shim must first state
which privileged component receives Windows's CPU_ON call and how AP entry
recovers the original X3; a UEFI-only proposal lacking that interception point
does not repair the observed contract.
