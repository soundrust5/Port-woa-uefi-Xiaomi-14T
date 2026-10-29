# Host-only AP context recovery trampoline, 2026-10-29

## Result and scope

The diagnostic implementation in `diagnostic/degas-ap-context-bridge/` restores
a published 64-bit context into X0 using MPIDR, without consuming incoming X0.
It then branches to the real AP entry saved in the same descriptor. The AP
code assembles to 448 bytes, including a 64-byte immutable configuration.
It requires EL1, little endian, and SCTLR_EL1.M/C/I all clear. It never enables
the MMU, alters a page table, installs an exception vector, changes a timer or
frequency, or calls another routine. Successful dispatch preserves X18..X30,
SP, all SIMD registers, all translation registers, and incoming DAIF. X1..X17
are scratch; X0 is the saved context. A rejected descriptor parks with
interrupts masked and no RAM writes.

This is a host-only consumer and publication model. No firmware, Windows
binary, boot image, GZ/TEE/LK/preloader image, phone state, or frequency setting
was changed. There were no phone commands, flashes or reboots. The binary in
`generated/` is an emulator fixture, not an image proposed for deployment.
Eight-core Windows is not established by these tests.

The missing integration is a legitimate producer that observes the real
Windows CPU_ON entry/context, retains the context before GZ discards it,
publishes this descriptor, and requests the trampoline entry through the
existing native provider. A separate candidate using Windows's existing
per-CPU ACPI parked-mailbox construction may permit the context to be known
before the native call; that requires an independently proved Windows path
and a real way to select the recovery entry. A generic trampoline does not
intercept an SMC and cannot recover a context that nobody saved.

## Evidence grounding

The root-cause and GZ audit reports establish that the active GZ CPU_ON guest
construction preserves the entry PC and target affinity while restoring
guest X0 from a zeroed register slot. The full-container source hash used by
the existing original-instruction reproduction is
`011c07ed744d69a7b2760a3fb2bdd727327bec08bc844dd4177086e4ce04ee0e`.
This implementation neither corrects nor patches those GZ instructions.

The local Linux reference at
`diagnostic/oneplus12/pcie/android-cesta-trace/kernel-reference/arch/arm64/kernel/head.S`
uses MPIDR and a known shared symbol in `secondary_holding_pen`, and obtains
secondary startup data from `secondary_data` after the MMU transition.
`smp_spin_table.c::write_pen_release` cleans/invalidates the published value
to PoC for observers outside coherency. This prototype borrows independent
affinity discovery and explicitly published shared data. It does not copy a
Linux task pointer, assume the Windows startup structure matches Linux, or
claim Linux's complete processor initialization is reproduced.

The Linux boot protocol separately requires the loaded image to be cleaned
to PoC and requires entering CPUs to share a coherency domain. Those are
platform requirements, not properties that a host emulator can establish.
[Linux AArch64 boot protocol](https://www.kernel.org/doc/html/v6.16/arch/arm64/booting.html).

Existing project assembly influenced the bounded contract:

- `CpuProbe.S` uses an independent literal before trusting context delivery.
- `Parking.S` obtains affinity through MPIDR and hands control to Windows
  through BR without a C runtime or stack.
- `WindowsObserver.S` explicitly guards EL1 and cache/MMU state and uses
  distinct code/data attributes when it installs temporary translations.
- `test_degas_cache_visibility.py` supplies controlled backing-RAM values
  and explicitly avoids presenting this as hardware cache emulation.
- `test_gz_psci_context.py` runs the pinned original GZ selected CPU_ON
  handler and final register-restore instructions, with external helpers and
  world-switch stack selection modeled.

## Immutable configuration and slot ABI

All fields are little-endian uint64. Code/config reside in one independently
allocated 4 KiB page; the table is a distinct retained 4 KiB page. Addresses
are physical. The fixture permits PA36, including addresses above 4 GiB.
Actual allocation must use reserved RAM that survives the Windows handoff;
none is allocated by this diagnostic prototype.

The configuration at `DegasApContextConfig` is located by ADR, rather than
through incoming X0. It is patched only before code publication and remains
immutable for the entire boot epoch.

| Byte offset | Field |
|---|---|
| 0 | Magic `0x4150435458425231` |
| 8 | Nonzero expected boot epoch |
| 16 | Aligned table physical address |
| 24 | Caller-approved executable entry range start |
| 32 | Caller-approved executable entry range end, exclusive |
| 40..63 | Reserved zero |

The table header has magic `0x4150434254424c31` at offset 0 and boot epoch at
offset 8. It occupies the first 128 bytes. Slot `cpu` starts at
`128 + cpu * 128`; slot 0 is unused. The 128-byte alignment separates all
slots for the modeled 16/32/64/128-byte D-cache line sizes. An integration
must derive the actual maintenance granule and reject a larger one or enlarge
the slot allocation; silently sharing a cache line is not allowed.

| Slot byte offset | Field |
|---|---|
| 0 | EMPTY=0, RESERVED=1, READY=2, CLAIMED=3, CANCELLED=4 |
| 8 | Canonical MPIDR affinity |
| 16 | Boot epoch, equal to header and immutable configuration |
| 24 | Real physical AP entry |
| 32 | Original full 64-bit context; zero is valid |
| 40 | Magic `0x41504342534c5431` |
| 48..127 | Reserved zero |

The only accepted canonical affinities are `0x100` through `0x700` in steps
of `0x100`: Aff3=Aff2=Aff0=0, Aff1=1..7. The mask is `0xff00ffffff`, retaining
Aff3 while excluding architectural U/MT flags. Invalid Aff0, Aff2, Aff3,
CPU0, and out-of-range Aff1 fail closed. This is the confirmed MT6897 topology,
not a general MPIDR hash or a permissive low-byte CPU lookup.

The configuration and table pointers are trusted allocated objects, not an
untrusted memory parser. The AP checks magic, nonzero epoch, pointer alignment,
PA36 table range, header epoch, slot READY state, full canonical affinity,
slot epoch/magic, aligned nonzero entry within the caller-approved executable
span, and exclusion of its own code/config page. It does not authenticate
arbitrary RAM, walk a physical memory map, or prove that a caller-supplied
executable span is valid. Bad aligned physical pointers could still abort;
the caller must construct and pin valid mappings and allocations.

## Publication, ownership and native semantics

The Python `PublicationModel` records the required publication operations; it
does not execute them on an Arm CPU. A real producer must implement them in
the correct caller translation regime with legitimate memory ownership:

1. Retain the original target affinity, real entry, and all 64 context bits
   before any GZ CPU_ON processing. Reserve a previously unused slot while
   holding a producer lock. Reject a second request for that CPU in the epoch.
2. Write RESERVED plus affinity, epoch, real entry, context and magic. Clean
   every descriptor cache line to PoC and execute DSB SY. The producer must
   similarly publish the immutable code/config and initialized header before
   their first possible use. Executable publication must include the required
   instruction-cache synchronization for any observers that can hold it.
3. Publish READY with release semantics, clean its cache line to PoC, and
   execute DSB SY before invoking native CPU_ON64. Native arguments in the
   general bridge model are `(C4000003, canonical_mpidr, bridge_pa, 0)`;
   the original context remains in the slot and is independent of X3 delivery.
4. The AP performs LDAR on READY, validates its descriptor, loads context and
   real entry, writes CLAIMED, executes DSB SY, restores incoming DAIF and
   branches to the real entry with X0 equal to the complete saved context.
5. Return the actual native CPU_ON status to the caller. The host model
   permanently cancels a READY slot on failure, including ALREADY_ON. Success
   is never forged. Native SUCCESS only means the provider accepted the
   request; the target can still reject/park before reaching Windows.

There is one physical consumer for each whitelisted MPIDR. The AP does not
use LDAXR/STLXR on default MMU-off Device data attributes. Its CLAIMED store
is not an atomic lock for arbitrary competing processors claiming the same
slot. Producer serialization and unique physical affinity are required.
Sequential repeated AP entry sees CLAIMED and parks. A fabricated concurrent
same-MPIDR emulator instance is outside the physical contract.

Neither native success nor a CLAIMED store authorizes slot reuse. A CPU_ON
failure also permanently consumes the slot in this deliberately bounded
prototype. A new epoch requires independent proof that every target is OFF
and no native CPU_ON is outstanding, followed by rebuilding/republishing the
table and immutable configuration together. The model requires an advancing
nonzero uint64 epoch without wrap. The `all_targets_off` argument is a host
fixture representation of external proof, not a hardware CPU_OFF check.
Reusing an epoch defeats the stale-boot check. Hotplug/restart/cancel races
with a live AP are not implemented.

## Cache and architectural limits

The AP requires M/C/I off and little-endian EL1 because this exact entry state
was observed in the project. It changes only DAIF temporarily, and writes
only the selected slot's state. It performs no cache operation and does not
enable an ad hoc MMU to obtain coherency. SP, X18..X30, SIMD, SCTLR, TTBRs,
TCR, MAIR, TPIDRs, VBAR, timer and GIC state are not changed. X1..X17 and NZCV
are scratch startup state; an eventual Windows entry integration must confirm
that it requires only the documented X0 input and relevant architectural
entry state. The AP tests explicitly verify the preserved general/SIMD state;
they model system-register reads and DAIF writes.

The intended caller mapping has Normal-NC, EL1 RX/read-only code, and separate
Normal-WB Inner Shareable RW/PXN/UXN data. `bridge.py` contains the corresponding
MAIR/PTE constants and permission checks. These are requirements for a future
integration, not installed translations. This prototype creates no Device
executable mapping. GZ stage-2 permissions/attributes and any higher-level
cache behavior still require platform qualification; SCTLR_EL1 alone cannot
prove them. An implementation must avoid incompatible cacheable/noncacheable
aliases while obeying the OS memory ownership contract.

READY acquire ordering and DSB do not pull another CPU's dirty cache lines
into RAM. The tests intentionally supply a valid RAM descriptor containing
an old context and show that the AP restores that old value. This is a
negative test of the tempting barrier-only repair: structural validation is
not cache coherence. A successful case with freshly supplied RAM proves
instruction logic only. The producer must clean the correct cacheable VA to
the required PoC and prove the target can observe it under the real GZ/cache
configuration. No dirty data owned by Windows may be invalidated blindly.

After publication the producer must not write, clean an obsolete dirty copy
over, or reuse the AP-owned state cache line. Reading an AP CLAIMED indication
through a cached producer alias needs a separately correct visibility
protocol; this prototype does not use that indication to recycle a slot.
All data remains retained for the epoch. The MCU/interconnect/coherency state,
cache maintenance traps, secure-world behavior and power transitions remain
outside Unicorn's proof.

The BSP-side interceptor, if chosen, must preserve its own ABI independently
of the AP code: original LR/SP, nonvolatile integer/SIMD state, all registers
that the actual SMC call contract preserves, and legitimate Windows startup
state. The host `PublicationModel` is not executable proof of that preservation.
Being able to jump with a saved X0 is not proof that an EL1 UEFI routine can
observe or replace Windows's later SMC call. No EL2 ownership, HVC provider,
GZ patch, or Windows runtime callback is introduced by this directory.

## Reproducible host validation

Run from the repository root:

```powershell
python diagnostic/degas-ap-context-bridge/build.py
python diagnostic/degas-ap-context-bridge/test_bridge.py
```

On Windows the builder invokes the existing WSL clang/ld.lld/AArch64 binutils.
On Linux use `python3` directly. Tests use the existing vendored Unicorn 2.1.4
and Capstone 5.0.7, pin the current assembly source and binary hashes, and
execute the assembled instructions rather than replacing the AP lookup with
a Python implementation. `generated/ap-context.disasm`, `.symbols`, `.json`
and `test-report.json` retain reviewable output.

Current binary SHA256:
`3c479a17a75cf7631179b85a767b8409e1d5bbdb3c36aa02c305c420c386b60f`.
Current assembly SHA256:
`6ab2b4e810260b38e80b26515e3cbcf896d70c610e6fe6d87bb8be0a9e96a3f0`.

All 11 test methods pass, with 109 assembled AP executions. Coverage includes
every AP affinity, zero/one/high/all-one contexts, high physical code/table/
entry placement, ignored incoming X0, rejected invalid affinities, empty/
reserved/cancelled/consumed descriptors, magic/epoch corruption, invalid entry
bounds/alignment/recursion, repeated AP entry, reset with stale READY bytes,
unsafe EL/EE/M/C/I state, preserved X18..X30/SP/SIMD/DAIF, strict descriptor
write bounds, duplicate/outstanding producer rejection, retained native
failure, explicit reset guards, cache line publication ordering and intended
Normal RX/XN data attributes. Backing-RAM scenarios explicitly include an
unpublished READY state and an undetectably stale valid context.

Six cases first run the existing pinned original GZ handler plus its actual
final restore instructions for CPU1/CPU7 and contexts zero/high/all-one.
The requested GZ entry is the bridge address. The original path yields X0=0,
the bridge PC and SPSR=3C5. Those output values feed the assembled AP fixture,
which restores the full saved context and reaches the real requested entry.
External GZ helpers and world-switch stack selection are still modeled; no
GZ bytes change and no secure-world or full-machine execution is claimed.

This establishes the bounded AP instruction algorithm under the stated
publication and entry contract. Runtime producer placement, actual cache
visibility, retained allocation, Windows integration and hardware validation
remain required before any claim of native Windows CPU startup repair.
