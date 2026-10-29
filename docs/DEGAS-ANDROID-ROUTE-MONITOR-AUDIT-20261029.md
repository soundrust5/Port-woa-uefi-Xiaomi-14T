# Android reset route and independent CPU monitor appraisal — 2026-10-29

Read-only audit except this report. No implementation or device operations.
Evidence: `evidence/raw/2026-10-29-degas-two-cpu-runtime-id-v1-result/`.
Runtime identity now matches the audited NT entry, DOS/PE signatures and
complete CodeView GUID/age. This supersedes the earlier uncertainty about a
different loaded build for this particular capture; it is not a full runtime
image hash.

## What the Android evidence establishes

The recovered console is 196317 bytes, contains its original wrapper P0,
and ends at successful EBS. The manifest says both pstore and debuglogger
copies are complete and identical. There is no fresh wrapper P0, Android
request DEBUG, or subsequent UEFI initialization in that retained console.
The valid AP journal proves that separate journal region survived, not that
every later console append was retained or that the console represents every
subsequent reset.

`WindowsBoot.h` deadloops if a boot application returns after EBS; it cannot
legally return to the menu. A usable firmware menu observed after Windows
would therefore require another invocation/reset or some unverified path.
The retained log does not show that invocation. A stale visible screen alone
does not prove that the menu's key loop is running.

An accepted Android route deliberately branches before `entry_ramoops.S`
and `diagnostic_ramoops()`: no new P0 is expected for that accepted branch.
But selecting BOOT ANDROID in a newly entered firmware menu should normally
have an earlier P0 from entering that menu, plus BootAndroid's request DEBUG.
Their absence prevents locating the reported failure in the present evidence.

## Source contract and unresolved causes

- `BootAndroid.h` requires the packaged header branch 0x14004000, publishes
  magic/inverse at 0x480AEF00 with fences and readback, logs the request,
  then arms a reset-only ten-second watchdog and deadloops.
- `degas_android_route.S` checks both magic words, consumes them BEFORE its
  EL1/MMU-off/D-cache-off/little-endian guards, and silently selects normal
  UEFI on guard rejection. Thus a valid request can intentionally be consumed
  without entering Android; there is currently no retained reason marker.
- Accepted route copies the trampoline to 0x44470000, then copies stock
  Image [0x40400000,0x42670000) to [0x40000000,0x42270000). The forward
  copy is correct for this overlap. The x0..x3 boot arguments and SP survive.
  Both code copies already have `DSB SY; IC IALLU; DSB SY; ISB` before
  branching; D-cache is guarded off. Stale instructions within these two
  copied ranges are not demonstrated, and an extra speculative cache flush
  is not an established fix. Initial execution of the LK-loaded wrapper still
  relies on LK's instruction publication/reset contract, before route code
  can run its own invalidation.
- `ramoops_probe.c` preserves a valid old ring and appends its entry; it
  initializes only invalid metadata. The serial sink never initializes a bad
  header and returns short instead. AP parking/journal code never writes the
  console ring. The current 196317-byte captured ring is below its 262132-byte
  capacity; ordinary wrap cannot explain keeping its oldest P0 while losing
  later append records.
- Windows prepares/releases AP1 in this two-CPU build, and has no AP shutdown
  operation on its way out. The design relies on the subsequent platform
  reset establishing a fresh boot. A leaked AP would be serious if reset
  failed to reset/quiesce it, but there is no post-Windows affinity snapshot
  demonstrating that. Initial pre-Windows OFF discovery is insufficient.
  Neither leaked AP state nor stale I-cache is an observed cause here.

No source correction is justified by this capture. The useful next diagnostic
distinctions are request actually committed, route actually entered, flag
accepted/rejected with EL/SCTLR, stock jump reached, and a truly fresh firmware
entry/CPU affinity set. These require independently retained bounded markers;
the existing P0 only covers the normal UEFI route. No experiment is performed
or authorized by this report.

## CPU2 monitoring KiBugCheckData while Windows uses CPUs0+1

Technically plausible as an explicitly experimental third active physical
CPU, not a transparent read-only observer. Keep CPU2 disabled/unadvertised
to Windows and give it separately reserved executable code, immutable private
tables and scratch/retained memory before EBS. Derive its own context from
its entry image rather than trusting PSCI's broken incoming context argument.
CPU2 must never enter Windows, call Windows/UEFI routines after EBS, use
Windows's stack/TPIDR/MMU, or write the shared serial console.

AP1's verified runtime base and bounded TTBR/TCR snapshot can supply the
identity-gated configuration. Reading kernel data with the old MMU-off
physical reader remains invalid: BSP dirty WB data was already proven
invisible to it. A private MMU with matching WB Inner Shareable RO RAM
attributes is necessary. The current qualified observer's post-load CIVAC
also publishes/invalidates other CPUs' lines, so repeatedly reusing that
reader would perturb Windows and cannot be called non-invasive. A coherent
load-only reader needs separate qualification before relying on it.

Concrete disassembly detail: PDB section24 gives KiBugCheckData RVA
0xDBB9A0 and KiBugCheckDataInitialized RVA0xC57000. At RVA0x25CB44,
Windows executes SWPAL of initialized=1 BEFORE the five data-word stores at
0x25CB74..80. Later code can rewrite bugcheck parameters/code. Therefore
initialized is an entry/ownership indicator, not a completed-record flag.
Reading it once and immediately committing the five words risks a torn tuple.
Repeated identical coherent five-word snapshots separated by barriers and a
bounded delay provide better observational evidence, still not an OS-defined
atomic-publication guarantee. Record timestamp, status and identity alongside
the raw words; distinguish timeout, read failure and stable data explicitly.

A per-CPU single-writer retained slot is suitable for a final result: prepare
metadata on BSP, write bounded payload and CRC, barrier, publish commit last,
and stop writing. Existing parking slots/decoder require matching AP/MPIDR,
mailbox and parking telemetry; a monitor payload needs an explicit schema
instead of masquerading as ordinary parking output. Repeated progress updates
need separate slots or commit invalidation/sequencing so reset cannot expose
a torn rewrite. The 0x480AEF00 Android flag remains outside monitor payload.

Risks requiring qualification include CPU2's own EL1 translation/exception
state, RAM whitelist and evolving Windows page tables, coherent cache effects,
CPU/cluster power/frequency changes from a third active core, and reset/CPU_OFF
cleanup. A masked-interrupt polling loop does not participate in Windows
processor power management. Bound its lifetime; cleanly stop or rely on a
verified whole-system reset. A nonzero tuple would identify what Windows
recorded, but would not by itself identify the faulting CPU or establish the
earlier NULL-write interpretation. The stable CPU7 one-core path stays intact.
