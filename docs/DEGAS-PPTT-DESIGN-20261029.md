# Degas processor/package/cache topology design — 2026-10-29

## Proposed first table

Install a **topology-only PPTT with one physical package and eight core leaves**.
Leave cache Type1 structures absent until complete geometry and ownership have
been qualified. This is an independently generated design; the root agent owns
firmware integration. No shared firmware, phone, native PSCI path, GICC affinity,
mailbox address, enabled-CPU count, frequency, or voltage was changed here.

Eight-core Windows operation is now user-confirmed on the retained native
context v2 image. The reported eight sockets and cache totals are a different
description problem. The current source supplies DSDT/FADT/MADT/GTDT and one
SMBIOS Type4 processor record. Type4 already describes eight physical cores and
eight threads, with unknown cache handles and unknown speeds; it does not
justify inventing eight physical packages or cache sizes.

The exact minimal table is216 bytes, revision3, with the existing OEM identity
style. All node offsets are relative to the start of PPTT:

| Offset | Structure | Length | Flags | Parent | ACPI Processor ID | Private resources |
|---|---|---:|---:|---:|---:|---:|
| 00 | ACPI description header | 36 | — | — | — | — |
| 24 | Package, Type0 | 20 | 01 | 0 | 0, invalid | 0 |
| 38 | Core0, Type0 | 20 | 0A | 24 | 0 | 0 |
| 4C | Core1, Type0 | 20 | 0A | 24 | 1 | 0 |
| 60 | Core2, Type0 | 20 | 0A | 24 | 2 | 0 |
| 74 | Core3, Type0 | 20 | 0A | 24 | 3 | 0 |
| 88 | Core4, Type0 | 20 | 0A | 24 | 4 | 0 |
| 9C | Core5, Type0 | 20 | 0A | 24 | 5 | 0 |
| B0 | Core6, Type0 | 20 | 0A | 24 | 6 | 0 |
| C4 | Core7, Type0 | 20 | 0A | 24 | 7 | 0 |

The package has only PhysicalPackage set. Its ID-valid bit stays clear because
the current DSDT has no corresponding package processor container. Each leaf
has ACPIProcessorIdValid and IsLeaf set, references the existing MADT UID, and
has ProcessorIsThread clear. Reserved fields are0. The checksum covers the
whole216 bytes. Microsoft publishes these exact node/flag layouts in its WDK.
[Microsoft node flags](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/acpitabl/ns-acpitabl-_proc_topology_node_flags),
[Microsoft topology nodes](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/acpitabl/ns-acpitabl-_proc_topology_node).

Do not set IdenticalImplementation on the mixed A510/A715 package. PPTT does
not carry numerical core-capacity weights or CPU operating points. Keep the
existing GICC efficiency classes and verified CPU identities; do not create
physical clusters or shared-cache groups merely from performance/DVFS policy
domains. A715 maximum-frequency binning is not a new physical package.

## ACPI requirements relevant to this design

PPTT is optional in ACPI; when present it must cover every MADT processor and
represent each physical package. Every leaf must belong to exactly one package.
The package flag denotes the physical boundary, including soldered SoCs. The
flat tree satisfies this without claiming unverified intermediate cache groups.

Cache structures are optional. Once supplied, however, they must describe all
processor caches, and Arm tables must supply all cache properties. A cache list
describes resources private to its owning hierarchy node; a shared cache belongs
to the common parent of its sharing CPUs. Its list terminates at that hierarchy
boundary rather than linking across owners. Consequently an L3-only cache node
with unspecified L1/L2 is not the proposed fix. These constraints come from
ACPI6.6 section5.2.31. [ACPI PPTT definition](https://uefi.org/specs/ACPI/6.6/05_ACPI_Software_Programming_Model.html#processor-properties-topology-table-pptt).

The table header uses current revision3. The pinned Windows leaf-binding code
also recognizes the revision2+ IsLeaf flag. The host package-walker tests cover
both revisions2 and3; this does not emulate complete firmware-table validation.

## Windows evidence beyond public type declarations

The Microsoft public PPTT/WDK pages establish supported structures; they do not
promise a specific Task Manager display for this board. The more concrete
evidence is the locally pinned, active kernel:

`artifacts/degas/cpu-audit/ntoskrnl.exe`, SHA256
`5bb49f8383e38b26ef0c18f3087db3be541e99143b80b92d2f83d824b7e5301c`.
Public PDB identity and active-file identity were qualified in prior audits.
Addresses below are RVAs in that build, not cross-version patch addresses.

- `HalpTopologyGetPackageNode`4A07E8 follows Parent at node+8 through the PPTT
  offset validator4A0B18 until Flags bit0 is set at4A080C/810. This is the
  physical-package boundary from the proposed tree.
- The processor-ID matcher4A0BC0 checks type0, ID-valid bit1, and ID at+0C.
  The revision2+ binding path4A0630/634 requires IsLeaf bit3. Those conditions
  match the proposed eight0A leaves and MADT UIDs0..7.
- `HalpTopologyQueryPackageId`4A1448 maps the GICC UID to a PPTT node, calls the
  package walker at4A14CC, then converts the resulting package node to its
  identifier. `HalpTopologyQueryProcessorRelationships`4A16E0 consumes the
  initialized PPTT node mapping when available. These are package/topology
  consumers, independent of SMBIOS's inventory string.
- `HalpTopologyQueryProcessorCacheInformation`4A1500 searches cache-resource
  lists in its initialized hierarchy. This establishes that Windows has a
  PPTT cache path, not that an incomplete cache list will be accepted or repair
  all fallback behavior.

`diagnostic/degas-pptt-design/test_design.py` executes the **original** package
walker and original offset-validator instructions in Unicorn. All16 cases
(eight leaves at each revision) return the same package node, table+24, with
status0. Only host fixture memory is supplied; no kernel bytes are patched.
This is a precise subroutine result, not a Windows boot or Task Manager test.

The expected first hardware result is one package containing eight logical
processors. Cache totals may still reflect architectural/Windows fallback.
There is no completed trace proving why the current display says32MB; eight
copies of a4MB description is a plausible arithmetic explanation, not a
confirmed Windows implementation trace.

## Cache facts, unknowns and ownership

MediaTek's Dimensity8300 page explicitly lists four Cortex-A715 cores, four
Cortex-A510 cores and4MB L3. It supplies no sets, ways, allocation policy,
write policy, cache IDs, or per-CPU sharing masks. The4MB statement is stronger
evidence than the current UI total, but does not alone establish every PPTT
property or the exact number of shared cache instances.
[MediaTek specifications](https://www.mediatek.com/products/smartphones/mediatek-dimensity-8300).

Arm documents A510 configurations with up to two cores in a complex and
configurable L1/L2 arrangements. This makes an assumed private L2 per logical
CPU unsafe. Equal CCSIDR values on two CPUs do not prove those CPUs share an
instance; different maintenance descriptions do not provide a sharing mask.
[Arm Cortex-A510 support](https://support.arm.com/compute-ip/cortex-a510).

The observed totals cannot resolve this. For illustration only,4x64KiB plus
4x128KiB is768KiB=786432 bytes. Similarly, four512KiB L2s plus four128KiB L2s,
or four512KiB L2s plus two shared256KiB L2s, both total2.5MiB. These are
arithmetic examples, not selected Degas cache configurations. The user's
reported786KB L1 remains unqualified.

For a later complete cache table:

- Proven per-core L1I and L1D receive separate resource lists at their core.
  A proven private L2 can be the common next entry of those lists, ending0.
- If A510 pairs share L2, introduce only the verified pair/complex parents and
  own those L2s there; keep eight leaf cores. Do not attach one shared-L2 node
  to multiple unrelated leaf-private lists and call that ownership proof.
- If one L3 instance is proven to cover all eight cores, it belongs to the
  package/common parent once, rather than eight private L3 instances. Do not
  infer that mask solely from the vendor's scalar size or from equal registers.
- Prefer unique, nonzero cache IDs and separate instance nodes. Do not compact
  identical private-cache descriptions when it would obscure instance identity.
  No known cache geometry/policy field is to be fabricated to fill the table.

Before serialization, also check the actual field widths: Size and Sets are
32-bit, Associativity is8-bit and LineSize is16-bit. Reject unrepresentable
values rather than truncating a CCIDX decode or substituting an API-specific
"fully associative" sentinel for an ACPI integer number of ways.

## What a safe raw register probe can establish

CCSIDR is an architectural **maintenance-parameter** description. Arm explicitly
does not guarantee that its sets/ways/line parameters describe actual
microarchitectural cache size. Its product is therefore a diagnostic value,
not sufficient authority for a physical cache declaration. Legacy CCSIDR bits
31:28 are UNKNOWN and must not be repurposed as reliable allocation/write-policy
inputs for PPTT. Selecting an unimplemented cache bank can produce undefined,
unknown or constrained-unpredictable behavior.
[Arm CCSIDR_EL1 definition](https://support.arm.com/documentation/ddi0601/2026-09/AArch64-Registers/CCSIDR-EL1--Current-Cache-Size-ID-Register).

A future explicitly scoped probe should:

1. Execute once on each already-running Windows PE, with migration excluded
   and a preallocated, per-CPU result slot. Record full MPIDR, MIDR, CurrentEL,
   CLIDR, ID_AA64MMFR2, original CSSELR and a validity/status field. The records
   must identify the same boot/build and physical affinity.
2. Enumerate only Ctype banks actually advertised by CLIDR, bounded to seven
   levels: instruction-only1, data-only2, split3, unified4. Select data/unified
   with InD0 and instruction with InD1. Do not blindly select all14 banks or
   undocumented tag-cache banks.
3. Save CSSELR, set the selected value, ISB, read the full64-bit CCSIDR, then
   restore CSSELR and ISB. Serialize against the same CPU's other users of that
   selector and preserve the caller's interrupt state. Perform no cache
   maintenance, cache disable, TLB operation, power change or frequency write.
4. Decode CCIDX from ID_AA64MMFR2[23:20]. Value0 uses ways[12:3] and sets[27:13];
   value1 uses ways[23:3] and sets[55:32]. Add1 to both encoded values. Line bytes
   are1<<(LineSize+4). Reject reserved CCIDX values; do not truncate to32bits.
5. Retain raw values and decode as maintenance geometry only. Include each
   selected bank's selector, architectural level and cache type. A trap or
   unsupported read must be a failed sample, not a zero-sized physical cache.

The host decoder uses these fields and includes extended-format boundary tests.
Arm's own cache-maintenance source confirms the CCIDX selector/ISB sequence;
this proposal does **not** execute its cache-maintenance operations.
[Arm TF-A cache helper](https://github.com/ARM-software/arm-trusted-firmware/blob/master/lib/aarch64/cache_helpers.S).

Windows `KeIpiGenericCall` is a possible collection mechanism: its callback runs
on all processors at IPI_LEVEL, so buffers and code must already be resident,
and the callback must perform no allocation, blocking or filesystem operation.
This is a design option, not a driver installed by this task.
[Microsoft IPI contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-keipigenericcall).

GZ owns EL2 and may trap or virtualize these registers. A successful EL1 read
is the guest-visible value, not independent proof of the underlying physical
cache. No read of EL2 or implementation-defined DSU/CPU registers is proposed.
Do not prestart APs with a new UEFI CPU_ON experiment to collect this data; the
working native boot ownership and AP-off handoff should be preserved.

## Verification and next boundary

Use a native ARM64 application to retain raw
`GetLogicalProcessorInformationEx(RelationAll)` output alongside Task Manager:
one RelationProcessorPackage with affinityFF, eight single-PE core records,
SMT flags0, and the existing efficiency classes. For caches, retain each
RelationCache level/type/size/line/associativity **and its affinity mask**;
do not compare only aggregate totals. Microsoft's API defines package and cache
relationships directly. [Microsoft topology API](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getlogicalprocessorinformationex),
[cache relationship](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-cache_relationship).

The present test artifacts are host-only:

- `pptt_design.py` generates and validates the216-byte revision3 package-only
  fixture, SHA256 `fef240c88e1496665c837016289b5814d03baca8328b3951868ca1c76f63091f`.
- `test_design.py`:5 tests pass, including16 original-Windows package walks,
  malformed-table rejection, both CCIDX layouts and legal CLIDR selectors.
- No live PPTT installation, Windows topology API capture or cache-register
  collection was performed by this agent.

The immediate justified firmware change is the package topology. A complete
cache table depends on independently supported physical geometry and sharing.
PPTT is a description and does not supply a DVFS/CPPC implementation; correcting
its socket tree must not be reported as changing CPU frequency or voltage.
