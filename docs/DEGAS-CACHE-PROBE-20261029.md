# Cache identification probe checkpoint, 2026-10-29

Cache work is frozen at the user's direction to prioritize CPU frequencies.
This checkpoint preserves a tested host prototype. It introduces no firmware
integration, device command, clock access, GZ change or actual cache-size claim.
It does not alter the working native Windows SMP configuration.

## Implementation and validation

`diagnostic/degas-cache-probe/CacheProbe.S` assembles to 912 bytes. Its returning
`DegasCacheCollect` leaf takes one owned record VA in X0 and returns status in
X0. It preserves X18..X30, SP and SIMD, masks DAIF while manipulating the local
selector, and restores the observed CSSELR_EL1 and incoming DAIF on every path
that returns. It changes no translation, cache enable, timer or frequency state.

CPU0 must use that returning leaf. AP1..7 have a separate wrapper that obtains
the record pointer from a private PC-relative literal, so missing or unrelated
incoming PSCI X0 does not affect discovery. After collection the wrapper masks
interrupts for its non-returning lifecycle and invokes native CPU_OFF. It checks
the current EL and complete canonical affinity first; canonical CPU0 cannot
reach CPU_OFF. An unexpected CPU_OFF return is recorded and parks the AP.
No native provider return is converted into synthetic success.

The probe reads MIDR, MPIDR, CurrentEL and SCTLR, then ID_AA64MMFR2_EL1, CLIDR,
the original CSSELR, and selected CCSIDR values. It accepts the two defined
CCIDX encodings. CLIDR types choose only existing instruction/data/unified
selectors with TnD=0, including independent I/D reads for a separate cache.
The first Ctype0 terminates enumeration; all higher types are ignored. A
reserved type before that point fails without selecting an invalid cache.
There are at most seven levels and fourteen CCSIDR reads. Every selector
write is followed by ISB before reading CCSIDR; restoration also uses ISB.

The AP mode requires little-endian EL1 and M/C/I off. The BSP leaf permits its
existing MMU/cache state and preserves it. Both depend on caller-owned valid
record memory. A malformed pointer cannot be validated as an allocation by a
stackless assembly routine; zero/unaligned pointers are rejected, while the
caller remains responsible for all other bounds and ownership.

The source/binary pinning builder and host tests pass: 11 test methods and
70 executions of the assembled instructions. They cover all eight affinities,
legacy/extended formats, high physical placement, separate I/D selectors,
fourteen-entry bounds, ignored higher types after Ctype0, reserved fields,
zero/high/lost incoming X0, CPU0 power-off exclusion, unexpected CPU_OFF return,
unsafe AP state, state restoration, modeled register traps and deadline expiry.
All register values/cache descriptions/power outcomes are synthetic. The
tests verify memory accesses remain inside code and the private record and
reject unwanted system operations. There are no AT, DC, IC, TLBI, HVC or clock
MMIO operations in the probe.

Current assembly SHA256:
`421cc91b9134b8fe956437265067bd75a46d35a7cc504de84e57b23f4934d159`.
Current binary SHA256:
`41b6fcac1f8aefbcab9e32730b213b62b755cf55b22016fa7abe7323bdd24d0e`.
The binary's AP entry is offset0, returning leaf offset208, and private mailbox
pointer literal offset904. `generated/cache-probe.json` is authoritative for
these offsets and source/binary pins.

## Record and bounded lifecycle

`ProbeAbi.h` describes a 512-byte private record. All fields are little-endian
uint64 except its final reserved bytes. Header fields are magic `DGCACHE1` at0,
length512 at8, nonzero attempt epoch at16, canonical MPIDR at24, and mode at32
(0=returning leaf, 1=AP wrapper). Header bytes40..63 are reserved.

| Offset | Result field |
|---|---|
| 64 / 72 | Last phase / status |
| 80 / 88 / 96 | Raw MPIDR / MIDR / CurrentEL |
| 104 / 112 / 120 | SCTLR / original DAIF / observed original CSSELR |
| 128 / 136 / 144 | CLIDR / ID_AA64MMFR2 / completed entry count |
| 152 / 160 / 168 | Incoming AP X0 / unexpected CPU_OFF result / completion |
| 176 / 192 | Error detail / last selector |
| 256..479 | Up to14 `(selector, raw CCSIDR)` pairs, stride16 |

Completion is invalidated first and published as `DONECACH` only after normal
collection/restoration completes. This is an ordered in-memory completion
marker, not a CRC or authenticated attempt identity. An eventual retained
capture must wrap these bytes in the established source/attempt-qualified
CRC journal. No such firmware integration was added.

Phase stores plus DSB SY precede the potentially trapping ID/selector reads
and selector/DAIF restoration stages. Selector phases encode
`(selector<<8)|20/21/22` in hexadecimal: select, read CCSIDR, entry stored.
Phase30 restores CSSELR, 32 restores DAIF, and33 completes the returning leaf.
The AP wrapper uses3F for power-off qualification,40 for CPU_OFF requested,
41 for unexpected return and42 for an unsafe power-off target.

`decode.py::monitor` models a caller-supplied deadline, default500ms. A stalled
phase or even completed geometry without actual AP OFF proof expires into a
reset-required result. The probe does not install an exception handler and
cannot restore state or force power-off after a register access traps. A real
integration would need a bounded BSP deadline, retained evidence, a recovery
watchdog, and independent native OFF confirmation before reusing memory or
starting Windows. A CPU_OFF-request phase or completion marker alone proves
neither power state nor successful coherency. CPU0's direct leaf cannot have
its deadline enforced by another ordinary BSP polling loop; its trap recovery
would require the hardware watchdog/exception policy.

## What the values establish

MMFR2.CCIDX[23:20]=0 selects legacy CCSIDR: ways=[12:3]+1, sets=[27:13]+1.
CCIDX=1 selects extended CCSIDR: ways=[23:3]+1, sets=[55:32]+1. Both formats
use line bytes=`1<<([2:0]+4)`. Other CCIDX values are rejected. Extended RES0
fields and legacy upper32 RES0 bits must be zero. Legacy bits31..28 are
preserved as unknown values and are not treated as write/allocation policy.
The decoder supports non-power-of-two ways/sets and the full field widths.

These are maintenance geometry fields. Their product is named
`maintenance_span_bytes`; `physical_cache_capacity_bytes` and `shared_cpu_ids`
remain null. Arm explicitly distinguishes these set/way parameters from actual
microarchitectural capacity. The probe therefore cannot independently qualify
a PPTT capacity, sharing relation, inclusivity, external/system cache, or CPU
frequency. The raw CLIDR LoC/LoUU/LoUIS/ICB fields are reported without turning
coherence levels into CPU-sharing IDs. See
[Arm CCSIDR definition](https://support.arm.com/documentation/ddi0601/2026-09/AArch64-Registers/CCSIDR-EL1--Current-Cache-Size-ID-Register),
[Arm cache hierarchy definition](https://documentation-service.arm.com/static/6166bf63e4f35d248467c9c0),
and [Arm MMFR2 feature definitions](https://documentation-service.arm.com/static/5fad544dca04df4095c1c90c).

The guest's register view may itself be mediated by GZ. EL2 controls can trap
ID/cache-register reads and CSSELR writes; successful emulation may expose a
virtualized view. Correct host control flow does not prove real hardware access,
truthful topology, physical cache capacity or sharing. The assembly makes no
attempt to change EL2 controls or catch/bypass those traps.

Any future integration would need separate retained code/result allocations,
correct Normal executable code and non-executable data mappings, code/header
publication to PoC before CPU_ON, correct BSP visibility of AP-written RAM, and
one writer per record. M/C/I off and DSB do not independently establish those
properties. No MMU setup or cache maintenance is hidden in this probe.
