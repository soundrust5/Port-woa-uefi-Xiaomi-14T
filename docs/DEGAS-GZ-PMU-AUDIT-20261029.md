# Bounded resident GZ PMU audit, 2026-10-29

**UNKNOWN as the cause of Windows's uniform NominalFrequency1300.** The
inspected core has no identified PMCCNTR emulation/scaling implementation.
Its PMU setup preserves inherited trap controls, so static inspection cannot
establish the actual runtime PMU state either. No CNTFRQ×100 explanation is
supported by this audit.

The source is the read-only full active capture
`evidence/raw/2026-10-29-degas-active-gz-identity/gz_b.full.img`, SHA256
`011c07ed744d69a7b2760a3fb2bdd727327bec08bc844dd4177086e4ce04ee0e`.
The inspected payload starts at raw200, length1B77C0, linked base38000000;
payload SHA256 is
`85802b8800ce948f36f463945dd9bf3dfd348073c771133b5f7e44d1baa57fd9`.
Addresses below are hexadecimal payload offsets; add200 for raw offsets.

## Findings

An aligned static core-payload scan found only three PMU register accesses:

| Offset | Original instruction |
|---|---|
| 3B534 | MRS X1,PMCR_EL0 |
| 3B538 | MRS X0,MDCR_EL2 |
| 3B548 | MSR MDCR_EL2,X0 |

The intervening instructions extract PMCR.N[15:11], apply
`old_MDCR & 0xFFFFFFE0`, and OR in N. Therefore this code sets HPMN to the
implemented event-counter count. It preserves inherited TPM[6]/TPMCR[5] and
the remaining low32 control bits; high32 bits are cleared by that mask.
It does not explicitly clear either PMU trap control. Those bit definitions
also match the repository's Linux `arch/arm64/include/asm/kvm_arm.h`.
PMU register traps use SYS64 syndrome EC18.
[Arm exception-syndrome reference](https://support.arm.com/documentation/111107/2026-09/AArch64-Registers/ESR-EL1--Exception-Syndrome-Register--EL1-).

The adjacent setup at3B4F8..508 enables the low two CNTHCTL bits and writes
CNTVOFF=0. It neither reads CNTFRQ nor constructs a 1.3GHz PMU value there.
No PMCCNTR or PMCR write instruction was identified in the scanned core.

The actual synchronous dispatcher reads ESR_EL2 at3CE88 and indexes its signed
halfword table at79D10 using EC−1. SYS64 EC18 uses index17 (decimal23), whose
entry at79D3E is41; `3CF5C + (41<<2) = 3D060`.

At3D060 the current-VM query selects two inspected routes. The no-VM/Linux
fallback at3D3E0 compares an explicit non-PMU sysreg allowlist. PMCCNTR_EL0's
masked ISS key30E41A and PMCR_EL0's30E418 both reach unhandled path3D474.
The GZVM route calls37924; the same keys reach its unhandled path379BC.
Neither route supplies a synthetic counter, a multiplication factor, or a
frequency result for these accesses. These are conditional routing results;
the current-VM query's live return value was not measured.

## Bounded reproduction and limits

Run `python diagnostic/degas-dvfs-audit/gz-pmu/audit.py` on the host. Ten cases
execute the original dispatcher/selected handler instructions: PMCCNTR and
PMCR read/write in both routes, plus two positive non-PMU controls. Twelve
additional cases execute the original PMCR/MDCR bit-manipulation instructions
and verify the resulting value, including preserved TPM/TPMCR. All pass.
`report.json` and `original-disassembly.txt` in that directory retain evidence.

ESR/PMCR/MDCR register inputs and the current-VM helper are modeled. Execution
stops before hardware passthrough, logging or abort helpers. No image bytes
change. This is not a full GZ, loaded-module, secure-world, dynamic-patching or
live-register audit. Inherited MDCR state, counter enable/filter/division,
hypervisor time, and Windows's actual selected timer rate remain unmeasured.
An absent software scaler in these paths does not prove physical PMU cycles
accurately reflect clock rate under every runtime condition.

The existing Windows instruction audit separately establishes that its
PMCCNTR reader is at203FD0 and its save routine rounds selected timer Hz to
MHz: 13MHz produces13, while 1.3GHz produces1300. These new GZ findings do not
identify where that selected rate originates. The uniform1300 report remains
insufficient evidence of a fixed physical OPP or a GZ-generated 100× clock.

No GZ/TEE/LK/preloader, Windows, shared firmware source, clock setting or phone
state was modified. No device command, reboot, PMU enable/reset or patch was
performed. Cache investigation remains paused.
