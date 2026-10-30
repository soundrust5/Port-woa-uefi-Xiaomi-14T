# Linux-style shared CPU context bridge for Degas — 2026-10-29

## Result

The Android/Linux technique can be adapted at the Windows bootstrap boundary:
publish context in shared memory, enter an AP stub that does not need incoming
X0, reconstruct X0, then continue the original Windows AP startup. It does not
require changing GZ if Windows participates in selecting or constructing that
stub. **No unmodified-Windows, UEFI-only interception mechanism is established.**

There is a concrete version-specific shortcut: the pinned Windows kernel
accepts a nonzero GICC ParkedAddress even when native PSCI is enabled. This lets
UEFI reserve predictable per-CPU startup mailboxes. Windows already writes the
complete state into those pages before CPU_ON. A Windows AP-entry modification
can recover the mailbox using MPIDR; alternatively a Windows CPU_ON-entry
selection modification can enter a per-mailbox physical thunk that obtains its
own mailbox by ADR. Neither option needs prestarted APs or fabricated success.

This report and `diagnostic/degas-linux-context-design/` are host-only artifacts.
No phone operation, reboot, firmware source edit, Windows image modification,
partition write, or GZ/TEE/LK/preloader change was performed. The known CPU7
single-core image remains untouched. Eight-core Windows is not demonstrated.

Read together with:

- `DEGAS-HAL-ROOT-CAUSE-20261029.md` for the confirmed parking HAL failure;
- `DEGAS-GZ-HANDOFF-CONTEXT-AUDIT-20261029.md` for the exact GZ register loss;
- `DEGAS-PARKING-TRANSITION-AUDIT-20261029.md` for the Windows physical stub.

All NT addresses below are RVAs in `artifacts/degas/cpu-audit/ntoskrnl.exe`,
SHA256 `5bb49f8383e38b26ef0c18f3087db3be541e99143b80b92d2f83d824b7e5301c`.
They are not addresses for another build. Runtime identity must match before
using these findings for any later integration.

## What Xiaomi's published Linux source actually does

The sparse local Degas source checkout identifies Xiaomi commit
`11f638af362bd6600a47fcb8e4739607fb118d36`, whose commit message names
Xiaomi 14T Android U. Its file objects are not locally populated, so the four
files below were read from MiCode's primary repository at that exact commit.
This is a source-level explanation; it does not assert a byte-for-byte match
between that published source and the installed Android kernel.

1. `cpu_psci_cpu_boot()` supplies the target's logical-map MPIDR and the physical
   `secondary_entry` address. Its interface has no caller-supplied context.
   The firmware wrapper invokes CPU_ON with the fourth argument explicitly
   zero. [Xiaomi arm64 PSCI startup](https://raw.githubusercontent.com/MiCode/Xiaomi_Kernel_OpenSource/11f638af362bd6600a47fcb8e4739607fb118d36/arch/arm64/kernel/psci.c),
   [Xiaomi firmware PSCI wrapper](https://raw.githubusercontent.com/MiCode/Xiaomi_Kernel_OpenSource/11f638af362bd6600a47fcb8e4739607fb118d36/drivers/firmware/psci/psci.c).
2. The BSP puts the incoming idle task into `secondary_data.task`, publishes
   boot status, invokes the CPU boot method, and waits for that CPU to become
   online. This is a shared singleton used with serialized bringup, rather
   than an opaque PSCI context. [Xiaomi SMP startup](https://raw.githubusercontent.com/MiCode/Xiaomi_Kernel_OpenSource/11f638af362bd6600a47fcb8e4739607fb118d36/arch/arm64/kernel/smp.c).
3. `secondary_entry` calls `init_kernel_el`, which constructs a boot-mode value
   in X0. After its own page-table setup, `__secondary_switched` addresses
   `secondary_data` directly and loads the idle task. It does not depend on
   original PSCI X0. The nearby MPIDR holding-pen loop is a different entry;
   this PSCI path calls `secondary_entry`. [Xiaomi secondary assembly](https://raw.githubusercontent.com/MiCode/Xiaomi_Kernel_OpenSource/11f638af362bd6600a47fcb8e4739607fb118d36/arch/arm64/kernel/head.S).

Thus Linux success is consistent with GZ discarding the guest CPU_ON context.
The transferable idea is independent discovery of published state. Copying
Linux's page-table/stack setup into Windows is unnecessary and would introduce
a second architectural-state restoration implementation. A Windows bridge
should restore the original mailbox pointer and let Windows restore itself.

These claims are scoped to this pinned Xiaomi source. They are not a claim
that every Linux version or future parallel bringup path ignores PSCI context.

## Windows already publishes the needed context

Let `D` be a 64-byte HAL processor descriptor, `M` the mailbox physical address,
`V` its BSP mapping, and `G` the common Windows physical startup entry.

| Boundary | Pinned instructions and effect |
|---|---|
| Allocate/map mailbox | `48A490`: nonzero input X0 takes the supplied-address branch at `48A4D0`; `48A4D4` stores M into D+18; mapping returns V, stored into D+20 at `48A4E8`. Zero input instead allocates a page and obtains its physical address at `48A4C4`. |
| Build state | `47F870..880`: copy 0x6D0 bytes to V+20. `47F884`: store V+20 into mailbox+10. `47F88C..890`: store G into mailbox+8. `47F894`: DSB SY. `47F898..8A0`: publish the descriptor's LocalID into mailbox+0. |
| Native CPU_ON selection | `47F8F8`: X2=M from D+18; `47F8FC`: X0=target MPIDR from D+30; `47F900`: call `498330` with X1 pointing to G. |
| Raw argument construction | `49834C`: X3=X2, preserving M as the PSCI context. `49835C`: X2=[X1], selecting G. The lower call reaches the initialized PSCI handler. |
| Guest AP after GZ | PC=G, X0=0 under the confirmed GZ behavior. The Windows thunk at original RVA `2000A0` branches through its patched physical-body literal. At original `2000DC`, the body computes X2=X0+20. |
| Original continuation | The body loads saved translation registers, initial TPIDR/X18 and stack from that state, loads the saved virtual state pointer from mailbox+10, enables the Windows MMU, then enters its existing continuation. |

The data required to repair this launch is **M**, not LPB.Prcb, a ready-queue
pointer, generic CONTEXT.X18, or a guessed kernel base. The full saved state is
already at M+20. Restoring X0=M before original `2000DC` preserves the existing
Windows state builder and scheduler initialization.

`G` belongs to a separate global page built by `498170`, which copies
`2000A0..200220` (0x180 bytes) into global-page+10 and fixes its literals.
`E06760` supplies the same G for each target. **The unmodified entry PC does
not identify a particular per-CPU mailbox.** ADR on G can recover the common
page but cannot recover M without another source of per-CPU identity/state.
The common page's first two words hold its address/table-root data, not an
existing MPIDR-to-mailbox table.

## A useful native-PSCI GICC behavior

The GICC parser at `48A8F4` loads ParkedAddress from GICC+18. At `48A8FC` the
set PSCI-capability bit branches directly to `48A90C`, bypassing the legacy
parking-version test. It calls `48A490` with that nonzero address. Consequently
this exact build can use UEFI-reserved mailbox pages while keeping
FADT.ARM_BOOT_ARCH=1 and GICC.ParkingProtocolVersion=0.

This is a binary-observed compatibility behavior, not a new standard firmware
interface. The generated Degas MADT has revision5. ACPI6.3 describes the field
as the parking mailbox address and requires parking version0 for PSCI-only
systems. ACPI6.6 additionally requires both parking fields0 for MADT revision7
and later. Do not claim that a future MADT revision or Windows build must
preserve this shortcut. [ACPI6.3 GICC fields](https://uefi.org/sites/default/files/resources/ACPI_Spec_6_3_A_Oct_6_2020.pdf),
[ACPI6.6 GICC fields](https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf).

Microsoft requires the native PSCI method on platforms supporting PSCI.
The proposed mailbox reuse keeps that native power-on call; it does not use
the failed parking wake operation. Its modified bootstrap still needs separate
validation and must not be presented as Microsoft-supported integration.
[Microsoft multiprocessor startup requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/uefi-requirements-that-apply-to-all-windows-platforms#uefi-arm-multiprocessor-startup-requirements).

## Minimal adaptations and their real interception points

### A. Fixed mailbox pages plus a modified common Windows AP entry

UEFI reserves per-CPU pages and supplies their addresses through GICC. It
publishes an immutable table of full masked MPIDR to M. APs remain OFF. Windows
builds each mailbox and calls native PSCI normally. Modified Windows physical
entry code finds M using `MPIDR_EL1 & 0xff00ffffff`, places M in X0 and branches
to its original physical body. A contiguous Degas allocation can use Aff1 only
after explicitly validating all other affinity fields and Aff1's range.

The required interception is **AP execution before original `2000DC`**.
Installing a table in ACPI/NVS is insufficient: the existing Windows entry
contains no lookup. Its copied template, its builder, or its resulting page
must actually be modified. The 0x180 copy and its body/continuation fixups must
remain correct. The rest of the page being zero is not, by itself, proof of a
safe arbitrary code cave or a working patch installer. A PC-relative table
inside an expanded copied stub is possible as a design, but is not implemented
by this report.

### B. Select a per-mailbox entry, then recover X0 by ADR

With a prebuilt thunk at M+800, native CPU_ON can select `entry=M+800` rather
than G. That thunk derives M from its own physical PC, loads the original G
from M+8, sets X0=M and branches to G. It must not apply the parking loop's
OS-owned mailbox clears. Windows has already published G and the state before
its genuine CPU_ON call. There is no need to start or park APs in UEFI.

One illustrative semantic substitution is replacing `49835C`'s `LDR X2,[X1]`
with `ADD X2,X3,#800`: X3 already contains M there. This would make the
per-mailbox physical PC self-identifying without a table or template expansion.
**This is not a deployable one-instruction patch claim.** `498330` also has a
direct caller at `48211C`, with independently prepared entry/context values.
That caller must be audited, isolated or preserved. Unknown indirect callers,
resume, hotplug and other power paths remain outside the demonstrated scope.
The safer general form is a Windows call-site wrapper limited to the audited
native AP-start path, with authenticated build-specific installation.

The available 0x800..FFF region is outside the audited 0x6D0 state copied to
M+20, but lifetime, other OS writers and execution attributes still require
qualification. UEFI's old UC/Device+XN mapping alone does not establish that
an MMU-off AP cannot execute a physical thunk: it describes the BSP's UEFI
mapping. Conversely, old parking success does not prove all aliases and
native-entry publication correct for this adaptation.

### C. General shared-record CPU_ON wrapper

If predictable firmware-supplied mailboxes cannot be used, Windows can wrap its
own CPU_ON invocation. Before the real SMC it records target MPIDR, original
entry and full 64-bit X3 in per-CPU persistent shared memory, then substitutes
a common recovery entry. The AP looks up its row, restores X0 and jumps to the
original entry. It forwards the real provider result unchanged. This is the
general Linux-style bridge, with a larger Windows integration surface.

Its precise interception requirement is **the Windows-side CPU_ON call before
the SMC**, plus the substituted AP entry. An EFI protocol is not consulted by
the original SMC, and EL1 UEFI cannot turn itself into GZ's EL2 trap handler.
Placing C above GZ instead would require privileged control already ruled out
by the boot-chain audit.

## UEFI-only versus Windows changes

| Work | UEFI alone can supply it? |
|---|---|
| Reserve persistent pages, publish GICC addresses, populate immutable thunks/tables, leave APs OFF | Yes, as a host-prepared firmware change; no such integration is made here. |
| Capture Windows's dynamically chosen X3 after ExitBootServices with no OS hook | No established mechanism. |
| Cause an unchanged Windows CPU_ON to use a different AP entry | No established mechanism; GICC ParkedAddress chooses M, not G. |
| Make the original Windows common AP entry reconstruct lost X0 | Requires a Windows bootstrap modification. |
| Change kernel code on disk | Requires a separate pinned-copy patch and boot-integrity acceptance path; not performed. |
| Have a UEFI loader modify loaded NT or the generated page in RAM | Still a Windows code modification, and requires a real, correctly timed loader/kernel hook. It is not supplied by allocating NVS or by a generic ExitBootServices event. |

No supported pre-kernel Windows customization hook covering this exact stage
has been established. A boot-start driver must not be assumed to run before
the affected AP start. Installing an unsupported post-load patch, finding NT
by RAM scan, or assuming firmware loaded-image handles expose winload's NT
allocation would conceal the central missing integration step.

## Publication and lifetime requirements

The bridge preserves the real CPU_ON state transition and error semantics.
APs must be physically/virtually OFF until Windows asks to start them. It must
not return synthetic success for ALREADY_ON, skip the HAL wait, patch the NULL
GIC callback, or repair scheduler fields. Those changes do not repair context.

Every shared record and instruction must be visible in the AP's initial access
regime. Audit the actual Windows mapping of a supplied mailbox and all aliases,
and publish firmware-written code/table data to PoC with appropriate instruction
cache maintenance before any CPU_ON. The original global stub already has its
own PoC publication path; its being present does not establish publication for
new firmware pages. `47F894` is DSB SY, not an independently demonstrated clean
of arbitrary WB mailbox aliases.

Use complete MPIDR affinity identity, unique records and 64-bit addresses. Do
not truncate context, mailbox addresses, or entries above4GiB. The general
record design needs release/acquire publication, attempt/generation ownership,
and a rule preventing reuse while an old AP could still consume the record.
The fixed-mailbox design avoids changing its context pointer per launch but
still needs correct lifetime across resume/hotplug. A single Linux-style global
context is unsafe if more than one launch can be outstanding.

The early thunk should be stackless, make no firmware-service calls, touch no
GIC/MMIO, preserve required registers, and leave architectural restoration to
the original Windows body. It must not reuse the old observer's private MMU
setup as a production startup mechanism. A failed identity/lookup should take a
bounded diagnostic failure route; this report's local WFE loop is only a host
fixture failure marker and is not a production error-reporting design.

## Host-only executed proof

`diagnostic/degas-linux-context-design/bridge.S` implements the general AP-side
lookup: ignore incoming X0, mask MPIDR, scan at most8 immutable rows, reject an
unknown or duplicate identity, load the entire saved context into X0 and branch
to its saved entry. It writes no memory, uses no stack and preserves X18-X30/SP.

Built with WSL clang/LLD and executed under Unicorn by `test_bridge.py`:

- 19 execution cases pass, including all7 Degas AP affinities;
- contexts0,1,32/64-bit boundaries and full-width high-bit patterns survive;
- MPIDR non-affinity flags are ignored while Aff3 is retained;
- unknown/duplicate identities, invalid row counts and zero/misaligned entries
  fail locally; memory writes and preserved-register changes are rejected.

The complete code/literal/table fixture is312 bytes, SHA256
`98ebeb1886d10a1bf88107b0ced1ecfb1261c1658cf8fb3964d7460c18b7d528`.
`out/results.json` records the cases. MPIDR is supplied at the MRS instruction;
the remaining lookup/load/branch instructions execute. This does not test a
Windows patch installer, the original Windows stub, cache coherence, GZ,
power-state transitions or phone hardware. It proves the limited shared-memory
reconstruction mechanism, not a working Windows SMP port.

The next host-only integration task is to choose A or B, pin its exact Windows
patch site and all affected callers, and execute the original argument-building
and AP-startup bytes around that specific substitution. A separately reviewed
installation/boot-integrity mechanism is required before any later device test.
