# Degas CPU thermal shared-memory qualification — 2026-10-29

The saved live Degas DT and pinned MediaTek source identify a real CPU thermal
shared-memory interface. The subsequent Windows observation described below
empirically qualifies an autonomous temperature producer for the tested
firmware. The current guard requires runtime liveness and conservatively caps
frequencies. This is not a formal firmware API or proof of autonomous firmware
thermal enforcement.
No phone command, MMIO read, thermal-policy write, controller write, PLL write,
voltage write or GZ change was performed for this audit.

## Exact resource and fields

`evidence/raw/2026-10-25-degas-ufs-baseline-v2/live-fdt.json` identifies
`/therm-intf@114000`, compatible `mediatek,therm_intf`, sole resource
`therm_sram` at physical `0x00114000`, size `0x400`. Its sole `reg-names` string
is `therm_sram`; there is **no therm_cputcm resource in this captured DT**.
The DTB SHA256 recorded by that extraction is
`ed50317b7653bb54d9269185bda362ca7d0ef7d997a6a5b74cc4d11ee5927894`.
The TSV equivalent is row336. Status is absent, which is not a disabled node.

[Pinned thermal_interface.c](https://github.com/MiCode/MTK_kernel_device_modules/blob/3c4ccd07d5624879555ec710c11bb8818e5cb5c6/drivers/thermal/mediatek/thermal_interface.c)
maps the named resource with `devm_ioremap_resource`. Its optional TCM mapping
is selected only if `therm_cputcm` exists. Therefore the source-compatible
layout for this DT is the SYSRAM/CSRAM layout in
[thermal_interface.h](https://github.com/MiCode/MTK_kernel_device_modules/blob/3c4ccd07d5624879555ec710c11bb8818e5cb5c6/drivers/thermal/mediatek/thermal_interface.h).
This thermal resource is distinct from the cpufreq request resources.

| Field | Byte offset from therm_sram | Address | Meaning supported by source |
|---|---|---|---|
| CPU temperature | `0 + 4*cpu`, CPU0..7 | `114000..11401C` | Signed32 thermal temperature; per-core export |
| DSU temperature | `98` | `114098` | Signed32 average-temperature field |
| DSU ceiling | `C4` | `1140C4` | Signed32 kHz; getter defaults to -1 before ready |
| CPU ceiling | `200 + 4*c`, c0..2 | `114200,114204,114208` | Cluster ceiling kHz; getter defaults to -1 before ready |
| CPU current-frequency field | `210 + 4*c` | `114210,114214,114218` | Shared frequency telemetry; not an independent edge counter |
| CPU maximum-temperature field | `220 + 4*c` | `114220,114224,114228` | Signed32 cluster thermal telemetry |
| CPU limit OPP | `260 + 4*c` | `114260,114264,114268` | Limit indication, not a frequency |
| UTC count | `27C` | `11427C` | Diagnostic counter; update period/semantics unproved |

Offsets/addresses are hexadecimal; memory accesses are32-bit. The table
describes an interface for qualification, not permission to access guessed
sensor registers. Temperature values follow Linux thermal millidegrees Celsius
conventions: the source uses25000 as its default and95000 for its target defaults.
The shared-memory producer's scale and invalid values still need checking
against the installed binary and observed thermal-zone readings.

Do not mix these offsets with the alternate CPU TCM layout. That layout moves
ceilings to`D4`, current frequency to`E4`, maximum temperatures to`F4`, OPP limits
to`124`, UTC count to`134`, and DSU ceiling to`220`. No TCM physical address is
established by the captured Degas node.

## Values that cannot serve as a Windows safety proof

`get_cpu_temp` returns25000 before `sw_ready` and for an out-of-range CPU.
After initialization it replaces `THERMAL_TEMP_INVALID` with a cached previous
value. The cache declaration `{25000}` initializes CPU0 to25000 and other
entries to0 under C initialization rules. Consequently a plausible-looking
`cpu_temp` value can be a fallback or stale cache. The raw cluster maxima in
`cpu_info` are read without that substitution.

`is_cpu_limit` treats a signed OPP value as limited only when it is greater
than0 and differs from`0x27bc86aa` (`CSRAM_INIT_VAL`). Zero therefore means
"not limited" under this helper, while the initialization magic also produces
"not limited". An uninitialized table must not be accepted as safe just because
this Boolean is0. The source provides no validity, freshness or coherent-snapshot
bit for the whole tuple. Do not use the magic as an assumed temperature invalid
constant; the two have different roles.

This Linux file reads CPU temperatures, ceiling/current/max-temperature/OPP
fields but does not establish who produces them. Lack of a Linux write in this
file is not proof of firmware autonomy. The file **does** send host policy to
shared memory: minimum OPP hints, active CPU mask, TTJ, power budget, board
temperature/headroom, emulation temperature, cooler values and policy flags.
Its probe also writes `TCM_BUF_OFFSET` to select the buffer. These host updates
cannot simply be assumed present under Windows, and an observer must not
repeat initialization or copy these stores. The95C target defaults are not a
qualified Windows critical threshold.

## Bounded Android validation for the phone owner

The source Makefile names module `thermal_interface` under
`CONFIG_MTK_THERMAL_INTERFACE`. First identify the installed provider: module
list, matching platform driver's module symlink, loaded GNU build-ID note,
vermagic and exact `.ko` SHA256. Capture the current live DT node's compatible,
reg and reg-names as bytes. Pin those to the saved DT and extracted module;
the existing cpufreq module identity does not qualify this other module.
If built in, pin the exact kernel build and its matching image/symbols instead.

Prefer existing read-only exports under `/sys/kernel/thermal/`:

- `cpu_info`:12 comma-separated values, ordered hints0..2, ceilings0..2,
  current-frequency fields0..2, cluster maximum temperatures0..2.
- `cpu_temp`:per-core values with the fallback/cache caveat above.
- `is_cpu_limit`:aggregate Boolean with the magic/zero caveat above.
- `dsu_ceiling_freq`, `utc_count`:auxiliary observations.

Record monotonic timestamps, three ordinary rounds separated by a bounded
interval, policy0/4/7 current/min/max frequency and available OPP lists,
thermal-zone types/temperatures, and the existing native exports. This performs
the driver's intended reads; it adds no new physical mapping or speculative
MMIO. No stress, governor, thermal, cooling, TTJ or frequency change is needed
for an initial sample. Repeating an unchanged sample does not prove freshness.
Any negative/sentinel/nonphysical temperature or invalid ceiling is a failure
of qualification, not a reason to raise a limit.

If raw values are necessary, disassemble the **exact installed module** first.
Use a temporary kprobe immediately after its existing `readl` instruction or
existing read helper return: capture the already-read result from the live
register, the validated byte offset, and ordinary-RAM base-pointer/mode metadata.
Alternatively pair entry/return probes of `get_cpu_ceiling_freq` (cluster in
X0 on AArch64 entry, result in X0 at return) and `get_dsu_temp`; these functions
already do their intended reads when the normal kernel calls them. A
`get_cpu_temp` return probe alone cannot distinguish cached/fallback values.
If helpers are inlined, determine each post-load site from actual disassembly.
No numerical instruction offset is authorized by the reference source alone.
Do not dereference an MMIO base in a probe fetch argument: that would be an
additional read rather than reusing the native read. Pair by task and call,
and remove only the temporary events created for this capture.

The next stage must show a noncached temperature/limit update path still
running after UEFI handoff, its publisher identity/liveness, and enforcement
when host thermal policy is absent. Android observations can qualify decoding
and identify native readers, but cannot by themselves prove Windows continuity.
Until that stage is established, this interface is telemetry, not sufficient
justification for Windows to request stock2.2/3.2/3.35GHz dynamically.

## Reproducible host result

`diagnostic/degas-dvfs-thermal/audit_thermal_layout.py` parses the saved DT and
pinned header, checks the sole named resource, bounds each field to its400-byte
hex resource, records source SHA256s and emits `thermal-layout.json`. It passes
on the current files. The copied sources and Makefile are from official MiCode
commit`3c4ccd07d5624879555ec710c11bb8818e5cb5c6`; no claim is made yet that the
installed thermal module was built from these bytes.

## Installed-module evidence and exact observation points

The phone owner subsequently captured
`evidence/raw/2026-10-29-degas-dvfs-thermal-android-v1/`.
`audit_active_thermal.py` checks the installed `.ko` SHA256
`f2e0d8af09a5129afa22c7d727d2d4d68b9d4d638d03e374c2d776c2351b721c`,
its build-ID`60a16f5fd2cc864a1f90a4550e3ff92f9b2021d0`, and exact equality of
the ELF build-ID note with the loaded-module note. This checks binary identity
separately from the reference-source pin. The disassembly and relocations are
in`active-thermal.disasm.txt`; symbol metadata is in`active-thermal-identity.json`.

The owner's `sysfs.txt` has ceilings2200000/3200000/3350000kHz and core
temperatures around35C, while the `CPU_MAX_TEMP` fields are91762/94405/94048.
Those latter values therefore cannot be treated as instantaneous cluster
temperatures. Their exact producer meaning (threshold, historical maximum,
or another quantity) is unproved here. DSU ceiling is666666666, an unusable
value pending publisher qualification. `is_cpu_limit=0` and`utc_count=0` do
not close any freshness/enforcement question.

The exact binary compares raw temperatures with signed`-274000`, hexadecimal
`FFFBD1B0`, then selects the previous-value cache on equality. Its shared-memory
read helper is at`.text+D08`. `tm_data` is`.bss+78`; `sw_ready` is its first
32-bit member and `is_cputcm` is member offset8. No offsets below are raw
runtime kernel addresses.

For normal getter callers, module-qualified kprobe locations are:

| Location | Existing value to capture before instruction executes |
|---|---|
| `thermal_interface:get_cpu_temp+0x18` (`.text740`) | `ready=%x8:u32 cpu=%x0:u32`, after ordinary-RAM ready load |
| `thermal_interface:get_cpu_temp+0x64` (`.text78C`) | `tcm=%x8:u32 cpu=%x19:u32`, after mode load |
| `thermal_interface:get_cpu_temp+0x7C` (`.text7A4`) | `raw=%x0:s32 cpu=%x19:u32`, after native MMIO helper returns, before sentinel/cache handling |

The sysfs`cpu_temp_show` **inlines** this getter. Reading that file therefore
requires its own verified sites:

| Location | Existing value to capture |
|---|---|
| `thermal_interface:cpu_temp_show+0xA0` (`.text1FBC`) | `ready=%x8:u32 cpu=%x27:u32` |
| `thermal_interface:cpu_temp_show+0x154` (`.text2070`) | `tcm=%x8:u32 cpu=%x27:u32`, CPU0..6 path |
| `thermal_interface:cpu_temp_show+0xDC` (`.text1FF8`) | same, final CPU path |
| `thermal_interface:cpu_temp_show+0x170` (`.text208C`) | `raw=%x0:s32 cpu=%x27:u32`, CPU0..6 path |
| `thermal_interface:cpu_temp_show+0xF8` (`.text2014`) | same, final CPU path |

All these fetches use registers only. They do not dereference MMIO in probe
arguments and add no sensor/shared-memory read. Only the phone owner may apply
the bounded probes after checking module identity and platform probe support.
Probe installation changes tracing state; no phone probe was installed by this
audit. A positive `sw_ready` proves Linux initialization only; that flag is
ordinary Android module RAM and supplies no Windows firmware liveness proof.

An alternative single observation point covers the existing shared-memory
reads in this module: `thermal_interface:readl+0x48` (`.textD50`), with
`raw=%x8:x32 addr=%x19:x64 caller=%x20:x64`. The preceding instruction atD4C
is`LDR W8,[X19]`; the probed instruction is the followingDMB. X20 retains the
canonical caller return address for the module's own IO trace callbacks.
This reuses that native load and gives its mapped VA and originating call site;
derive caller-relative locations from the pinned module's runtime text base.
Keep the capture bounded to the intended sysfs read window. This probe does
not itself provide a CPU ID, so use the verified call sites/sequence and the
per-core probes when validating core association.

The initial version of `diagnostic/degas-dvfs-windows/degas_thermal.c` supplied a read-only400-byte
mapping and a bounded15-read telemetry snapshot (8 raw core temperatures,
3 ceilings,3 OPP-limit fields and UTC). Its initial safety API`DegasThermalRead` returned
FALSE with zero outputs for every domain. The telemetry snapshot explicitly
set`FreshnessQualified=FALSE`. This deliberately prevented a plausible stale
Android temperature array from authorizing Windows frequency increases.
`build_thermal.sh` successfully compiles this observer with ARM64 WDK headers;
`windows-thermal.disasm.txt` records the generated instructions.

## Windows autonomous-update observation and current conservative guard

The phone owner's
`evidence/raw/2026-10-29-degas-windows-dvfs-observer-v2-retry/DegasDvfs.log`
and`thermal-liveness.json` contain120 completed samples from55375ms through
200078ms (about145s). Every one of the8 core temperature words changed117..119
times, with99..109 unique values per core and readings around44..56C. The raw
CPU ceilings were2200000/3200000/3350000kHz. Only the read-only Windows observer
mapped these words; Android was absent. This empirically establishes an
autonomous live producer under Windows for this firmware and handoff. It does
not identify its firmware binary, promise an ABI, or prove that firmware itself
will enforce a requested ceiling under every fault condition.

The updated observer retains the same read-only mapping and15 native-format
reads, now performed only by`DegasThermalPoll` in the backend's PASSIVE worker
(normally every200ms). The reads happen outside the cache spinlock. The lock
protects only a bounded cache update; no polling loop, wait or allocation occurs.
`DegasThermalRead` runs at up to DISPATCH_LEVEL using that cache and monotonic
interrupt time, without MMIO. Initialization maps the resource and returns
without waiting for producer qualification.

Every core must supply a plausible0..115000mC value (including rejection of
the-274000 sentinel), at least2 changes, and a change span of at least100ms
after Windows initialization or requalification. If any one of8 cores becomes
invalid, has no change for2s, or the worker snapshot is2s old, the guard returns
FALSE with zero outputs for every domain. A stale source requires qualification
again. This deliberately treats a genuinely steady temperature as unsafe after
2s because this interface has no proved producer sequence/heartbeat word.

The guard uses the maximum core temperature within each CPU domain. Cold
operation permits only a raw ceiling within that domain's measured hardware
range, bounded by stock2200000/3200000/3350000kHz. At60C it applies caps of
1300000/1300000/1200000kHz; at70C it applies480000/400000/400000kHz. Cooling
hysteresis is5C: the hot stage releases at65C and the soft stage releases at55C.
The resulting cap always respects a lower firmware ceiling and is never raised
to invent an OPP. Invalid raw ceilings fail closed; the backend resolves legal
OPPs and reacts to a failed guard by lowering all domains. `CPU_MAX_TEMP`, UTC,
and the Android module's ready flag do not authorize a transition.

`degas_thermal_policy.h` is the shared implementation used by the driver and
mock-time host tests. `test_thermal_policy.c` covers startup, compressed changes
below100ms, all8 individual invalid sensors, the exact2s cutoff, stale recovery,
clock rollback, worker age, one frozen core, OEM range rejection, lower firmware
ceilings, both throttle stages and both hysteresis releases. The host tests and
ARM64 WDK compilation pass through`build_thermal.sh`.
