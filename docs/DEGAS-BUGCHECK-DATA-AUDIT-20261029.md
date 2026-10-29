# Bugcheck data observation audit — 2026-10-29

Read-only static audit; no monitor implementation or hardware operation.
Kernel/PDB are the same saved image identified in the AP release audit.
Fresh runtime identity capture in
`evidence/raw/2026-10-29-degas-two-cpu-runtime-id-v1-result/runtime-identity.json`
matches entry instruction, DOS/PE headers and full RSDS GUID+age; that
attempt's NT base is FFFFF8037A200000. This is not a full runtime image hash.

## Exact globals

| Public symbol | RVA | Width / interpretation | That attempt's VA |
| --- | --- | --- | --- |
| KiBugCheckData | DBB9A0 | Five consecutive U64: code, P1, P2, P3, P4 | FFFFF8037AFBB9A0 |
| KiBugCheckDataInitialized | C57000 | U32 initialization claim flag | FFFFF8037AE57000 |
| KiBugCheckActive | D9796C | U32 owner/phase/recursion state | FFFFF8037AF9796C |

All three reside in the image's zero-initialized .data tail. Addresses must
be derived from the **current same-attempt validated base**; never reuse
the example VAs after a reboot. Public spelling is KiBugCheckData, not
KeBugCheckData.

## Ordering and HAL5C applicability

`KeBugCheckEx` RVA25D7C0 sets X5=0 and calls `KeBugCheck2` RVA25CA90 at
25D7D0, preserving W0=code and X1..X4=parameters.

Within KeBugCheck2:

1. 25CB44 performs `swpal w8,w8,[KiBugCheckDataInitialized]`, exchanging 1
   with the flag. This is a claim of initialization, **not completion**.
2. The first claimant (old flag=0) writes:
   - 25CB74: `stp code64,P1,[KiBugCheckData]`;
   - 25CB7C: `stp P2,P3,[KiBugCheckData+10]`;
   - 25CB80: `str P4,[KiBugCheckData+20]`.
   Code is zero-extended from W0; subsequent entrants skip these stores.
3. Later, 25CBF4..25CC20 or 25CC40..25CC74 atomically claims
   KiBugCheckActive using `casal`, publishing `(PCR.Number << 4) | 3`.
   Low two bits=3 identify the active owner state; high bits above bit3
   encode the owner number here. Bits2..3 subsequently track recursive
   owner entry (incremented by 4 at 25D5F0).

HAL callers that invoke KeBugCheckEx with W0=5C therefore traverse this
tuple-publication path. There is no 5C-specific argument rewriting in the
audited KeBugCheck2: the special-code dispatch falls through the generic
path; with X5=0 from KeBugCheckEx, it reaches 25D22C without changing the
tuple. The previously inspected HAL release failure callsites directly
call KeBugCheckEx. Consequently this is an appropriate location to obtain
the numeric HAL stop tuple rather than transcribe the screen.

## Required read discipline and limit of the flags

Do not accept Initialized=1 alone: the observer can see it before any tuple
store. For a single normal bugcheck entrant, an **acquire** load observing
Active lowbits=3 after the release CASAL also observes its prior tuple
writes. Perform this acquire operation on the coherent WB alias of the
actual global; a Device/uncached physical read is not an equivalent test.

The observer should bracket complete 40-byte reads with acquired Active
and Initialized samples, require Initialized=1 and Active lowbits=3,
require nonzero code, and compare at least two complete tuples with
unchanged flags across an interval. Record raw flags, samples/timestamps,
walk statuses and stability result, not only a synthesized HAL tuple.
Continue sampling until the bounded deadline or a deliberately specified
settling interval so an initial partial sample is not immediately committed.

There is **no unconditional completion flag among these three globals**.
With concurrent bugchecks, CPU A can claim Initialized, pause before
finishing the tuple, and CPU B can skip the stores and claim Active first.
Acquiring B's Active publication does not publish A's unfinished stores.
Even repeated identical tuples alone cannot mathematically exclude a
paused partial writer. This limitation must remain explicit. A stable
capture is strong diagnostic evidence for the ordinary single-entrant HAL
case, not a formal transactional snapshot under arbitrary recursive or
concurrent failure. Capture a different nonzero code as evidence too;
requiring 5C could hide a changed failure mode.

## Firmware-owned hidden AP monitor assessment

The approach is feasible as an isolated diagnostic, provided the same
qualified coherent reader and private translation environment are used.
Keep all monitor code/tables/stack/journal memory reserved from Windows,
all kernel aliases read-only with compatible WB/shareability attributes,
and the monitor's retained slot private. KiBugCheckData occupies 40 bytes
and crosses a 64-byte cache-line boundary, so one load/pair or one-line
flush is insufficient. Its five fields do remain within a single 4KiB page.

The monitor needs valid current kernel translation information as well as
the base; a base alone cannot safely translate the globals. Publish these
inputs using a same-attempt release/acquire handshake, validate table walks,
and exclude stale retained state. A later boot phase can change mappings;
do not infer continued validity from the initial PC walk alone.

A CPU omitted/disabled in ACPI is not protected from all architectural or
firmware events. Broadcast SGIs, reset/watchdog, or power-controller actions
can still affect it; use private exception handling and a bounded counter
deadline without Windows services. Mask ordinary interrupts on the monitor
and avoid WFI-dependent wakeups. A Windows bugcheck freeze should normally
target Windows-known processors, but no assumption that the hidden AP is
immune to every freeze/power mechanism is justified by this kernel audit.

Do not issue live Windows cache clean/invalidate operations in the poll
loop. Use the qualified coherent reads; cache maintenance can alter the
failure and does not supply tuple atomicity. Clearly distinguish timeout,
walk fault, changed/unstable data, and stable tuple outcomes. A timeout
does not prove Windows never invoked KeBugCheckEx.
