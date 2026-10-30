# Degas parking transition audit — 2026-10-29

Read-only source/disassembly audit, except this report. Read the 2026-10-28
ACPI parking audit and end checkpoint first. No phone access, flashing,
reboot, firmware edits, or changes to the stable CPU7 single-core path.
Addresses below are RVAs in `artifacts/degas/cpu-audit/ntoskrnl.disasm`
(subtract preferred image base 0x140000000 from its displayed addresses).

## New disproof: the audited Windows physical stub is published to PoC

The routine at RVA 0x498170 constructs a separate global startup page:

- 0x49818C..198 zeros page bytes 0x10..FFF.
- 0x49819C..1A0 sets page+0 to its physical address and page+8 to the
  startup translation-table root.
- 0x4981A4..1C8 copies RVA 0x2000A0..0x200220 (0x180 bytes) to page+0x10.
- 0x4981CC..218 patches its physical-body and virtual-continuation literals.
- 0x4981F0 sets W1=0x1000; 0x498224 sets X0=page VA; 0x498228 calls
  RVA 0x25C1D8.

That last routine iterates `DC CIVAC` across the supplied page (0x25C200),
then `DSB SY` (0x25C210), and performs conditional instruction-cache
invalidation followed by `DSB SY; ISB` (0x25C214..26C). Therefore the
audited build does not simply copy a dirty WB trampoline and release an
MMU/cache-off AP without a PoC clean. This substantially weakens that proposed
cause. It does not prove that this exact routine ran in the failing boot.

The retained entry `0x15EAF7010` fits this page+0x10 construction. A physical
entry outside the loaded image is expected and is not evidence of another
kernel build. Likewise patched literals differ from on-disk bytes normally.

## Register-contract hypotheses disproved for this stub

The copied entry starts with `ADR X1; LDR X1; BR X1` (RVA 0x2000A0).
Its body at RVA 0x2000C0 immediately overwrites X1 with DAIF and uses
X0+0x20 as the saved-state address. At 0x200130 it loads X18 from
mailbox+0x40, writes TPIDR_EL1, and page-aligns X18. It does not obtain
X18 from the generic CONTEXT.X18 slot. Thus zero CONTEXT.X18 is insufficient
evidence of a broken AP context, and preserving a firmware-inherited X18
cannot repair this path.

`Parking.S` passes the mailbox physical base in X0 and branches to its saved
entry in X1. Its observer saves/restores both at FB0/FB8 and X18 at F48;
the journal call saves/restores X0/X1 through X19/X20. Marker uses X2..X8.
The final clear changes only mailbox+8, leaving mailbox+0x10 (virtual state)
and saved state intact. The body loads the virtual saved-state pointer from
mailbox+0x10 before enabling the Windows MMU and branching to its virtual
continuation. No new X0/X1/X18 clobber was found.

## Observer and firmware publication

`Parking.h` already cleans tables, cleans/invalidates observer and mailbox
code to PoC, and invalidates the BSP instruction cache before CPU_ON.
`WindowsObserverMap.h` describes code as Normal-NC RX, ordinary whitelisted
RAM as WB RO Inner Shareable, and mailboxes/journal as Device RW XN.
The observer restores SCTLR, TTBR0/1, TCR and MAIR and preserves X18..X30.
Its post-load CIVAC changes system visibility intentionally; its snapshots
cannot establish what a completely uninstrumented boot would observe.
The previously reported GCD/GetMemoryMap-versus-CPU-attribute uncertainty
remains; this audit provides no new evidence that it caused the failure.

## Follow-up tests

1. Capture the global startup page selected by mailbox+8 after validating
   physical bounds. Record page+0, page+8, the 0x180-byte stub at +0x10,
   and its literal slots +0x20/+0x28 (and hypervisor slot +0x128 if used).
   Compare against the audited image after applying these explicit fixups;
   do not treat differing embedded addresses as a build mismatch.
2. Pair a bounded physical instruction/literal snapshot with a qualified
   coherent WB snapshot of exactly the same page, sampling physical first.
   Equality would disprove dirty-BSP visibility for that captured stub at
   that instant. The observer currently samples PRCB/table lines, not this
   global startup page; its successful walks do not answer this test.
3. Establish runtime kernel identity separately: validate the LPB module
   list and actual loaded kernel's PE timestamp, SizeOfImage and RSDS
   GUID/age, plus active BCD device/path/kernel selection. The retained
   installed-file SHA match proves only that installed file matches the
   cached file. An alternative loader-selected image remains possible,
   but has not been demonstrated. The unexplained HAL tuple is a reason
   to verify identity, not proof of a different build.

No new demonstrated handoff defect or justified firmware/kernel fix resulted.
The concrete advance is the traced PoC publication and rejection of the
incoming-X18/CONTEXT.X18 and physical-address-as-build-mismatch hypotheses.
