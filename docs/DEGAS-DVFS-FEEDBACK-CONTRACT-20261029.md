# FxPPM frequency feedback

Read-only audit, 2026-10-29. No provider, firmware, counter-control or phone
changes were made. This establishes the software contract; it does not qualify
a physical counter source.

The captured FxPPM binary and matching public-symbol identity are recorded in
[the selection audit](DEGAS-DVFS-FXPPM-SELECTION-20261029.md). All addresses below
are RVAs in that exact binary unless marked NT. NT is the captured active image
`evidence/raw/2026-10-29-degas-active-boot-identity/ntoskrnl.exe`, SHA256
`5bb49f8383e38b26ef0c18f3087db3be541e99143b80b92d2f83d824b7e5301c`.
Exact bounded disassembly is retained in
`artifacts/degas/dvfs-windows/feedback-contract-excerpts.txt`.

## Descriptor and query

The smallest descriptor set accepted by the inspected FxPPM path is one
frequency counter. A performance-only counter does not satisfy its frequency
requirement. `InitPepPerfStates` 35090..351E4 validates nonzero NominalRate,
Type 0 or 1, Counter 0 or 1, zero reserved bits, and rejects DiscountIdle=1
unless Affinitized=1. It records the first frequency index and fails if none
exists. This is a necessary contract, not a guarantee of subsequent activation.

For a local architectural-register source, the prospective descriptor is:
Affinitized=1, Counter=FREQUENCY (0), Type=RELATIVE (1), Reserved=0,
NominalRate equal to the nominal processor frequency in **MHz**. DiscountIdle
must be selected from qualified hardware/idle semantics, not used to disguise
an unqualified measurement.

`QUERY_FEEDBACK_COUNTERS.Count` is an input. Preserve it and fill the advertised
number of descriptors. `PepNotifyQueryFeedbackCounters` 35698 allocates
4+8*N bytes, stores N in the supplied buffer and later copies exactly N*8
descriptor bytes. Changing Count is not a way to negotiate capacity.
The [Microsoft query definition](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/pepfx/ns-pepfx-_pep_ppm_query_feedback_counters)
agrees with this observed behavior.

## Type and units

For Type=INSTANTANEOUS, FxPPM `PepInstantaneousFrequencyRead` A2B0 reads the
64-bit InstantaneousValue at feedback buffer+8 and computes
`InstantaneousValue * 100 / NominalRate` at A2F4..A310. Frequency values must
therefore be MHz, not kHz or Hz. A software target copied from CSRAM does not
meet the delivered-frequency contract. A genuine hardware frequency meter
could meet it only with a bounded read; a timed busy loop in this callback
would not be acceptable.

For Type=RELATIVE, `PepDifferentialFrequencyRead` A1D0 forwards NominalCount
and ActualCount unchanged to NT. No nominal-rate or timer-frequency conversion
occurs there. FxPPM registers normalization 65536 for relative counters and
100 for instantaneous counters at 31718..31740.

NT `PpmPerfFeedbackCounterRead` 2D2578..2D25A8 computes
`65536 * (ActualNow-ActualPrevious) / (NominalNow-NominalPrevious)` and stores
the new cumulative pair. The live update path 2C6464..2C6494 does the same.
The provider must supply cumulative, consistently scaled values. Do not reset
the hardware on read. One sentence in the public FEEDBACK_READ remarks says
to reset relative counters; following it literally conflicts with this exact
installed NT path and with the incrementing-pair descriptor definition.

The [Microsoft descriptor definition](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/pep_x/ns-pep_x-_pep_processor_feedback_counter)
specifies equal nominal/actual increments at nominal frequency and MHz for a
frequency counter's NominalRate. Thus raw 13 MHz architectural timer ticks
cannot serve directly as NominalCount against actual core cycles. A candidate
pair would require explicit conversion such as
`NominalCount = timerTicks * nominalHz / timerHz`, with ActualCount in core
cycles, correct divider/filter handling, and safe 64-bit arithmetic. Its
reference frequency must also agree with the performance capability's nominal
frequency; the relative wrapper does not repair inconsistent references.

## Affinity, execution and validation

Affinitized=1 is the documented on-target-CPU contract. FxPPM copies it into
the kernel descriptor at 316EC..316F4. NT `PpmInstallFeedbackCounters`
51A5CC selects the target processor group affinity, raises to DISPATCH and
seeds the initial pair through 2D2508. NT runtime checks affinity requirements
at 2C639C..2C63B8. Its update path masks interrupts at 2C6208..2C6210 before
calling the feedback function at 2C6454. The callback can therefore perform
qualified local MRS reads without scheduling a remote call, but must use
resident code/data, no waits, allocations or interval measurements.
These constraints also match the
[Microsoft notification contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/ppm-notifications).

`FEEDBACK_READ.CounterIndex` is input. Validate it against the advertised
descriptor count, preserve it and write only the selected output union. The
actual buffer is 24 bytes on ARM64: index at 0, union at 8, second relative
value at 16. The
[Microsoft read definition](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/pep_x/ns-pep_x-_pep_ppm_feedback_read)
identifies those input/output fields. FxPPM's A3A8 wrapper logs a failed
notification but its caller still consumes the prezeroed output; transient
counter failures cannot be made safe merely by returning FALSE after advertising
support. Availability must be qualified before advertising and remain valid
through expected processor power transitions.

## Physical qualification still required

Read-only PMCCNTR plus a reference counter is only a candidate. Verify that the
read cannot trap, the cycle counter is enabled, its divide/filter configuration
and width are known, Windows does not repurpose/reset it, and values remain
usable through idle/resume. No PMU enable/reset should be smuggled into a
feedback read. AMU requires independent per-core availability/erratum checks.

If core cycles stop in idle while the reference continues, the pair measures
active cycles per wall time. It cannot be labeled active clock frequency
without matching idle accounting. NT copies DiscountIdle into feedback state
(51A644..51A64C), but this bounded audit does not establish its complete idle
correction semantics. Setting the bit alone is not evidence that the pair is
correct. Likewise QPC/CNTFRQ alone measures time, not delivered core frequency.

No feedback descriptor should be advertised until the actual physical source,
idle treatment, ownership and monotonic behavior are qualified. The existing
working eight-CPU/no-BSOD provider remains unchanged by this report.
