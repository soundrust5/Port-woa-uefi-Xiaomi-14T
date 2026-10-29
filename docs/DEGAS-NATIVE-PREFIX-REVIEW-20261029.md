# Independent native AP prefix review — 2026-10-29

**Conditional GO for a bounded CPU0+CPU1 cold-boot experiment. No GO for a
general SMP/power-management release.** This judgment covers the exact prefix
and its native mailbox contract. The new EBS locator/write integration is
separate work and is not certified by this review. No phone action or source
edit outside this report was performed; the other agent's files were not edited.

Reviewed `diagnostic/degas-windows-context-bridge/bridge_fixture.py`,
`NativeContextFixture.h` and `test_bridge.py` against the original saved active
kernel and its disassembly. Pinned NT SHA256:
`5bb49f8383e38b26ef0c18f3087db3be541e99143b80b92d2f83d824b7e5301c`.

## Prefix and actual HAL copy layout

No instruction or layout defect was found in the 56-byte substitution at
RVA2000C0..2000F8. It retains the DAIF.I entry condition and original DAIFSet#1
operation. The replacement TBZ loops on itself when I is clear; the original
uses a separate self-branch. Both refuse that entry condition indefinitely.

The original builder498170 copies exactly180 bytes from2000A0..200220 to the
new global page+10. Consequently source2000C0 executes at physical-page+30.
The replacement ADRP with immediate0 correctly obtains that page's base after
the copy, including above4GiB. Both existing body/virtual-continuation literal
slots and fixups remain at their original offsets. No copied length changes.

The replacement produces X0=mailbox, X2=mailbox+20, X1=common-page,
X4=[common-page+8], and X5=X4+800 before joining unchanged code at2000F8.
The original zero/ORR/ADD sequence has the same X5 result. No flag-producing
instruction is added; no stack or additional architectural-state write appears.
The original Windows body still performs its own subsequent state restoration.

The supplied tests cover the original context-loss fault and patched prefix
parity. Independently, six additional parity cases used common physical page
15EAF7000, CPU0/1/7, and incoming X0=0 or an unrelated high address. All matched
the original valid-context prefix through its first saved-state reads. This is
an emulation result, not evidence of physical coherency or Windows startup.

The fixture comment calling page+0 “virtualpage” should not be used as an
independent type proof. The important reviewed property is page+8's startup
translation root; the prefix consumes that same word through the same address
as the original. No behavioral discrepancy follows from that comment.

## Required topology and mailbox preconditions

The three-bit UBFX intentionally accepts only this fixed topology by external
precondition. It does not reject incompatible MPIDRs. Before enabling the patch,
the integration must establish for every potentially started CPU that
`MPIDR & 0xff00ffffff == i<<8`, i=0..7: Aff0/Aff2/Aff3 must be0, Aff1 must be
in0..7, with no duplicate affinities. Non-affinity U/MT flags may differ. Checking
only Aff1 or only the advertised CPU count is insufficient; Aff1=9 aliases CPU1.

Eight contiguous owned pages must survive ExitBootServices, with page-aligned
base B>0 and B+8000<=100000000. Every enabled GICC must supply ParkedAddress
B+(i<<12); parking version stays0 and FADT ARM_BOOT_ARCH stays1. CPU0+CPU1 alone
are enabled for the proposed experiment. APs remain OFF until Windows CPU_ON;
the prefix does not change CPU_ON entry selection or its real return status.

Native NT publishes each supplied mailbox through the existing47F830 path:
state at+20, virtual state pointer at+10, common entry at+8 and LocalID at+0.
This patch requires those actual pages, not merely a parallel firmware table.
The kernel's PSCI handler, GIC callback and HAL acknowledgment logic remain
original. The failed parking integration is not repaired or reused as a wake
operation by this change.

## Other consumers and power transitions

The common startup page is not cold-boot-only:

- `HalpProcessorOn`4820C0 calls47F578 at482108. That helper returns the same
  global entry G from E06760 and the selected processor descriptor's mailbox M
  from+18. Its CPU_ON call at48211C therefore retains the reviewed G/M contract.
- `HalpProcessorHaltPsciCpuSuspend`4986C0 also calls47F578, at498710. The raw
  default-suspend path passes G/M from that result; an alternate suspend wrapper
  receives the same pair. The earlier halt preparation481F3C invokes47F830 for
  its saved processor state. No different context type is demonstrated here.
- `HalInitializeOnResume`481C70 cleans the common page at481CA4. It does not
  reconstruct the original template or undo the replacement.
- `HalpInterruptQueryProcessorRestartEntryPoint`498260 exposes G through
  HalPrivateDispatchTable+278, installed atA8D72C. External/indirect consumers
  were not exhaustively closed. A query returning G alone does not prove that
  every hibernation/restart consumer uses this experiment's fixed mailbox.

The inspected direct power paths do not reveal a new incompatible context,
but this is insufficient to approve sleep, hibernation, hotplug, fast startup,
Hyper-V/VBS or runtime CPU power-off. The first experiment must remain a fresh
cold boot with no such transition. Its results cannot establish those features.

## Publication and integration gates

The original common-page builder already cleans its constructed page to PoC
and performs instruction-cache maintenance. The EBS integration must separately
publish its **source-template modification** before Windows later copies it,
and reserve/publish all mailbox pages consistently with their OS-visible memory
descriptors. DSB alone is not a clean of a dirty WB alias. The supplied mailbox's
actual Windows mapping/cache policy remains a hardware qualification item.

The loaded-image check must identify one exact relocated NT allocation in
validated loader-owned RAM, reject ambiguity or incomplete coverage, verify
the complete original normalized template and related native call guards,
and write only the56 reviewed bytes. Physical backing and kernel virtual
relocation base are distinct; a high DIR64 pointer must not be dereferenced as
a physical address. Cache publication and write/readback verification are
required after the write. An identity failure permits no speculative patch.

The patch must be opt-in for the native experiment's selected boot route,
with no persistent Windows disk change and no activation on the stable CPU7,
Android or old parking routes. A build-time/host test passing does not establish
that an ExitBootServices callback found the final NT template before HAL used
it, or that Windows integrity policy accepts the in-RAM change. Those are
explicit experiment observations, not assumed facts.

Subject to these gates, the prefix is a reasonable bounded test of the stated
context-loss mechanism. It is not a demonstrated eight-core fix and provides
no basis for changing GZ, TEE, LK or preloader.
