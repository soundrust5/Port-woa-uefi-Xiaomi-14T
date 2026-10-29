# Confirmed HAL stop and native context-loss causes, 2026-10-29

## Latest result: Windows native eight-CPU workaround works on hardware

The v2 cold boot has now succeeded: the user explicitly reports **8 logical
processors in Windows Task Manager**, substantially improved responsiveness,
and no longer saturated CPU at the idle desktop. Evidence of this human
observation is retained in `artifacts/degas-native-context-v2/user-observations.json`.
The exact v2 source ZIP, linked EFI modules, boot image and build manifest are
preserved there. Native v2 retained installation record is not yet captured;
the phone is deliberately left running Windows.

V2 boot SHA73e9b38c5514aba600aa48a37afc210d2264167489663b861ba402368059550a,
builda6663247-1cad-4fbb-919d-6e651cfd095c. No GZ/TEE/LK/preloader change.
This validates cold startup and user-visible eight-CPU operation; longer-run
kernel integrity, suspend/resume, hotplug and physical frequency control
remain separate unqualified items. The reported Task Manager1.3GHz has not
been established as the physical limit of every CPU.

## Earlier two-CPU proof and exact retained installation evidence

The original causes described below remain valid, but a reversible Windows-side
bootstrap adaptation now avoids the GZ context loss. The user confirmed **two
logical processors in Windows Task Manager**, a desktop, and faster completion
of OOBE. No GZ/TEE/LK/preloader change was needed.

The matched retained record is
`evidence/raw/2026-10-29-degas-native-context-v1-result/native-record.json`:
build9e1b2f2a-0c08-4656-9977-474fca3775d7, sequence1, stage3, status0,
one exact loaded NT match, write_attempted1, patched1, advertised_cpus2.
Physical NT backing140000000 and relocated VAFFFFF802AE400000 were separately
qualified. No actual stage1 access failure was recorded.

The native FADT PSCI flag remains1 and APs remain OFF until Windows CPU_ON.
Firmware supplies fixed per-GICC NVS pages; 56 bytes of the pinned NT's loaded
AP template recover X0 from MPIDR and those pages before continuing original
Windows setup. This mirrors Linux's memory-based bootstrap principle without
changing the resident provider or Windows files on disk. Working sources and
linked EFI modules are preserved with the v1 boot image and source manifest.

That first record proves installation of the two-core adaptation; the later
v2 user report above confirms eight logical CPUs. Sleep/resume, hotplug and
long-term kernel-integrity acceptance remain unqualified. See
`docs/DEGAS-NATIVE-CONTEXT-EXPERIMENT-20261029.md` for the integration and limits.

The EBS watchdog remains an early recovery measure. The existing Windows UFS
driver disables it in FindAdapter before normal disk writes, explaining why
this successful boot did not reset after30seconds. Do not promise automatic
reset after storage initialization.

## Hardware-confirmed Windows stop

Fresh retained capture:
`evidence/raw/2026-10-29-degas-bugcheck-monitor-v2-result/bugcheck-source.json`.
Attempt `26df86da-b228-4a71-8000-000000000001`; both AP1 and AP2 commits,
CRC, source identity and same-attempt runtime NT identity validated.

- Stop tuple: 5C / 203 / 4 / FFFFF78A00001860 / D.
- HalpInterruptLastProblem=D.
- LastProblemStatus=C0000001.
- LastProblemController equals the stop's controller pointer.
- Source `minkernel\hals\lib\interrupts\common\start.c`, line1386.
- Runtime NT base FFFFF800D3C00000; matching CodeView GUID/age.

This selects the failed processor-start path, not the alternative raw-SGI13
error path. It precedes the AP acknowledgment wait. A waiting/acknowledgment
failure is instead logged as problemE at start.c1380.

## Why the current Parking experiment produces this HAL

1. PrepareParking clears FADT ARM_BOOT_ARCH, declaring PSCI absent.
2. HalpPowerPsciInitDiscard B2C3C8 tests the ARM_BOOT_ARCH halfword bit0 at
   B2C3F4/F8 and returns without initializing HAL's PSCI context when clear.
3. HalpInterruptStartProcessorPsci47F8C8 sees the clear capability and returns
   C00000BB before any CPU_ON call.
4. HalpInterruptStartProcessor A8C2D0 attempts the controller's fallback+80.
   This kernel's GIC3 registration4ACAB0 supplies NULL there, so default
   C0000001 is retained and problemD/start.c1386 is recorded.
5. HalpInterruptInitSystem A8D7AC later reports 5C/203/controller/problem.

An AP can still see the already published mailbox and enter our observer;
that does not make the BSP's failed wake operation successful. This is a
defect of the chosen Parking integration, not evidence of a failed physical
CPU. Merely advertising PSCI while retaining prestarted APs is not a fix:
CPU_ON can then return ALREADY_ON, also rejected by this startup path.

Earlier queue observations remain observations, not established primary
causes. No Windows NULL dereference, allocator or scheduler pointer is patched.

## Exact resident provider loses native PSCI context

Active TEE and GZ were read from block devices without modification:

- `evidence/raw/2026-10-29-degas-active-atf-identity/manifest.json` confirms
  ATF SHA ef838aa754b367a0cfef4d18272d812895bb28aa55525b6a672245f223ca4935.
- `evidence/raw/2026-10-29-degas-active-gz-identity/full-container-manifest.json`
  confirms the COMPLETE active gz_b image, including modules/certificates,
  SHA 011c07ed744d69a7b2760a3fb2bdd727327bec08bc844dd4177086e4ce04ee0e.

ATF's decoded standard CPU_ON path stores the caller context into entry
arguments and copies it into the NS return X0. GZ's Linux-guest CPU_ON64
handler at payload3D6D8 zeroes the target guest register context at+1000,
sets PC/SPSR but never copies original guest X3 from caller+18 into targetX0.
Its actual return chain loads X0 from that zeroed+1000 slot before ERET.
This explains the qualified hardware result: 14 AP executions, zero context
matches. See DEGAS-GZ-HANDOFF-CONTEXT-AUDIT-20261029.md for exact addresses.

`diagnostic/degas-prcb-walk/test_gz_psci_context.py` executes the original
handler and register-restore instructions with external services modeled.
20 cases cover CPU1/7 and five64-bit contexts in original/model modes.
Original restoresX0=0; a deliberately labeled behavioral MODEL storing
guestX3 after memset retains the full context, PC/SPSR and unrelated fields.
This is not a deployable GZ binary patch or a full-hypervisor emulation.

## Privilege, HVC and signing boundary

GZ launches the guest at EL1 and already owns EL2. The existing EL1 UEFI
cannot install an EL2 PSCI relay. Active unmap2 registers HVC owner4 for vendor
operations; VERSION/FEATURES unmatched IDs return [2,ID,0,0], not standard
PSCI support. The prepared query-only prototype was therefore removed before
build/flash; no pointless HVC/CPU_ON phone experiment was performed.

The retained boot log proves GZ is authenticated, with SBC enabled and
certificate-chain/header/image checks, BEFORE LK and fastboot start.
The container has cert1/cert2 and additional authenticated modules after the
code payload. It is not free padding and has no established unsigned-patch
or fastboot recovery path. No GZ/TEE/LK/preloader image was changed or flashed.

Eight-core Windows is NOT fixed. The root cause is now localized; repair
requires a real context-preserving provider/earlier privileged mediation, or
a separately designed Windows bootstrap compatibility approach. No supported
EL1-only replacement was established. Do not claim that toggling a FADT flag,
an extra cache barrier, or blindly returning success solves this contract.

## Device state at the earlier bugcheck-monitor checkpoint

Stock boot_b restored; Android ADB working. Windows partitions/files, GPT,
GZ/TEE and frequency settings unchanged. Working fullscreen CPU7 image remains
`artifacts/degas-prime-fullscreen-v1/boot.img`, SHA256
d4297ff9f41b894087ec259ecfa5bf141404bb0ee43fcc950bbb85213291485e.
The last diagnostic image was bugcheck-monitor-v2; no new image flashed after
its capture. No shell sessions, mounts or further phone tests are pending.
