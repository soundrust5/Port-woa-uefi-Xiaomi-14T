# Read-only Windows AMU probe

The v4 driver adds a boot-only AMU diagnostic. It does not advertise feedback
counters, change PEP performance selection, enable/reset counters, change
thermal control, or add any timer/watchdog behavior.

`degas_amu.c` probes the eight active Windows processor indices at PASSIVE_LEVEL.
It selects/restores each processor affinity, reads MPIDR/PFR0 first, and only
reads AMU registers when AMUv1/v1p1 is reported. Core and constant enable bits
must both already be set. Every register read is inside SEH; affinity restore
uses a finally block. There are at most three 1 ms active-stall samples per CPU,
with ISBs delimiting the measurement. There is no sleep or counter-control
write. MPIDR Aff1 must uniquely cover UID0..7 with Aff0/Aff2/Aff3 zero.

Qualification requires increasing core/constant/architectural counts, reference
frequency12..14 MHz, constant delta within20% of architectural delta, bounded
arithmetic, and resulting100 MHz..6 GHz measured frequency. These are probe
sanity bounds, not platform operating limits. Exact configured reference Hz,
raw counts and feature/enable registers are exported for independent review.

Snapshot ABI version4 is1340 bytes. The entire prior556-byte prefix is retained.
At offset556 `Amu` contains InitStatus, QualifiedMask, ProcessorCount,
ProbeComplete and eight96-byte records indexed by Windows processor index.
Each record contains Status, Uid, Mpidr, Pfr0, Enable, ReferenceHz, MeasuredKhz,
Attempts, CoreBefore, ConstantBefore, TimerBefore, CoreAfter, ConstantAfter,
TimerAfter. ULONG fields are32 bits; raw register/count fields are64 bits.
The appended ABI is packed to4-byte alignment. Logger v4 writes all eight
records once on the first completed probe snapshot; `_hi`/`_lo` suffixes retain
all64 raw bits without CRT formatting.

Status codes:0 valid sample,1 unsupported feature,2 counters disabled,
3 register exception,4 reference frequency invalid,5 counter did not advance
or wrapped/reset,6 reference/sample ratio implausible,7 arithmetic overflow,
8 MPIDR identity invalid,9 Windows topology invalid,10 not probed. All-eight
sample success produces mask255 and InitStatus0. A probe failure does not
block the existing driver/backend/PEP initialization.

Host verification: the actual shared math/identity qualifier tests cover all
eight UID mappings, other-affinity rejection, feature encodings,1.3/3.35 GHz
calculations, ±20% boundaries, zero/stopped/wrapped counts, implausible reference,
overflow and ABI sizes/offsets. Full ARM64 driver and logger compilation pass;
existing actual DPM/work handler host tests pass. ARM64 object disassembly is
retained at `artifacts/degas/dvfs-windows/degas_amu.disasm.txt`.

A successful short active sample proves only those sampled register reads and
rates. It does not prove long-term ownership, suspend/resume continuity, idle
semantics, erratum freedom, or readiness to publish a runtime PEP feedback
counter. Actual device results must be reviewed before that separate change.
No phone staging or hardware actions were performed by this implementation.
