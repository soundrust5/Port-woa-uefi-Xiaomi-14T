# Degas Windows DVFS audit — 2026-10-29

The current evidence supports a missing Windows performance-control integration,
not a proved physical1300MHz limit. The safe next step is read-only telemetry
of the existing controller and independent counters. A direct `_CPC` mapping
to the MediaTek request word is only a conditional design candidate; its units,
arbitrary-request behavior, Windows ARM64 provider support, resident firmware
and thermal contracts have not been qualified. No clock, voltage, performance,
enable or PLL write is authorized by this audit. No phone command or reboot
was performed, and the working SMP bridge was not edited.

## Captured facts and reporting distinction

`evidence/raw/2026-10-29-degas-cache-frequency/manifest.json` explicitly labels
the capture as Android sysfs after Windows/reset, not live Windows measurement.

| Domain / physical CPUs | Android available frequency range | Captured policy ceiling | cpuinfo_cur / scaling_cur at capture |
|---|---|---|---|
| 0 / CPU0..3 | 480..2200MHz | 1400MHz | 1400 / 1400MHz |
| 1 / CPU4..6 | 400..3200MHz | 2300MHz | 2056 / 1200MHz |
| 2 / CPU7 | 400..3350MHz | 2400MHz | 2258 / 400MHz |

Those values were read sequentially, so differences alone cannot identify
which path measures hardware or exclude a transition between reads. They do
show why policy requests, ceilings and instantaneous reports must remain
separate. A3350MHz available OPP does not imply that current thermal/power
policy permits it. Android temperatures in the same capture are also not
Windows temperatures.

Saved Windows Kernel-Processor-Power records report NominalFrequency1300 and
PerformanceImplementation0. SMBIOS clock fields were zero. Existing
DegasRuntime uses GetActiveProcessorCount and CallNtPowerInformation, labels
frequency values OS_reported, and already accommodates eight CPUs. Microsoft
defines CurrentMhz using a nominal/throttle relationship; this API is not an
independent clock meter. [Microsoft PROCESSOR_POWER_INFORMATION](https://learn.microsoft.com/en-us/windows/win32/power/processor-power-information-str)

### The claimed CNTFRQ13MHz ×100 explanation is unproved

The exact active NT SHA256 is
5bb49f8383e38b26ef0c18f3087db3be541e99143b80b92d2f83d824b7e5301c.
Its matching public symbols and bytes establish:

- HalpCycleCounterDiscover49FB48 registers a PMCCNTR_EL0 reader at203FD0 as
  timer type10. HalpCycleCounterInitialize49FBF0 initializes that PMU path.
- HalpTimerSaveProcessorFrequency488A20 selects timer type10, reads its
  frequency at timer+C0, and stores round(Hz/1,000,000) at PRCB+774 and rawHz
  at PRCB+778. Its literal at488A88 is1,000,000.
- HalpTimerMeasureProcessorsWorker489170 contains counter-delta measurement
  against reference timing. PpmPerfGetCurrentFrequency2D2648 returns a default
  percentage100 when performance state structures are absent; that100 is not
  independently a CNTFRQ-to-core-clock multiplier.

`diagnostic/degas-dvfs-audit/audit_windows_frequency.py` executes the original
save-routine instructions in Unicorn with only external timer lookup modeled.
All six cases pass; `windows-frequency-test.json` retains exact disassembly
and outputs. A13,000,000Hz input produces13MHz; a1,300,000,000Hz input produces
1300MHz. No selected runtime timer frequency, PMU virtualization behavior or
complete later nominal-frequency path was captured. Thus these results do not
prove the ultimate origin of1300, but the numerical coincidence with13×100
is not sufficient evidence. Neither this audit nor the current Task Manager
display establishes a fixed boot OPP.

## MediaTek request path, pinned to the available source

The local MiCode repository HEAD is
3c4ccd07d5624879555ec710c11bb8818e5cb5c6, branch bsp-duchamp-u-oss.
It is an official same-MT6897 reference. The active CPU-frequency module has
now been independently pinned and decoded as described below; that does not
establish source identity for every other installed Degas module.

The main driver uses domain offsets00 for a32-row LUT,84 for enable,88 for
requested performance,8C for readiness, and114 for transition latency. LUT
frequency is low12bits in MHz. CSRAM+4CC selects index versus kHz requests.
Its `.get` reads the request word; it does not count actual clock edges.
Initialization writes enable1 and checks CPUFREQ/SVS readiness bits. These
operations must not be copied into a read-only observer.
[Pinned MediaTek main driver](https://github.com/MiCode/MTK_kernel_device_modules/blob/3c4ccd07d5624879555ec710c11bb8818e5cb5c6/drivers/cpufreq/mediatek-cpufreq-hw_main.c)

### Exact active module and Android request capture

`artifacts/degas/dvfs-active-modules/source-identity.json` records the active
vendor_boot match, SHA256
eb5a09e9ee7a50039492f0d28fdc2eb07baadbb7102a5ac79c293b9c47edaf2e.
The extracted ramdisk00/mediatek-cpufreq-hw.ko has SHA256
f12addd9a3b5c677e8fec7b11903561e2d4073dff831a5ce4bd4dc7057df51fc.
Its entire ELF .note.gnu.build-id is byte-identical to the separately captured
loaded-module note; GNU build ID is
068e6f5000761002c3541edbe177a210f2299352. Vermagic is
6.1.138-android14-11-gdd83a2647cf4, module name mediatek_cpufreq_hw.

`diagnostic/degas-dvfs-audit/active-cpufreq.disasm.txt` records the exact
functions and relocations. Section-relative addresses are not absolute runtime
kernel addresses:

| Active .text offset | Verified operation |
|---|---|
| C04, mtk_cpufreq_hw_get | Reads domain[CPU], then mapped register pointer at domain+18 |
| C30 | Calls the existing readl_relaxed helper for that request word |
| C34, get+30 | X0 holds the raw read result; values0..31 index the table, otherwise in-range kHz is returned directly |
| A40, target_index | Index path writes index; frequency path loads the table frequency; optional FDVFS call retained |
| AB8, fast_switch | Frequency path writes the original target-frequency argument, while its return value uses a resolved LUT entry |
| 888, cpu_init | Reads latency, writes enable1, then reads readiness and checks bits0/1 |
| 258..278, driver_probe | Reads CSRAM+4CC, stores a mode flag, then unmaps the global CSRAM mapping when nonzero |

The compiled byte named freq_scaling_disabled at .bss+118 has inverted storage:
byte1 selects the kHz path, byte0 selects index. The independently captured
fdvfs_enabled byte is at .bss+11C. These semantics come from the actual branch
instructions, not interpretation of the variable's name. The cached flag
establishes Linux's selected mode; it is not a new read of CSRAM+4CC.

The root-collected bounded trace is
`evidence/raw/2026-10-29-degas-dvfs-requests-v1/capture.txt`, SHA256
0678c054e6908de9af890246eb3ac5f16fe9bff86809d8ee53c0f0c5d35c63d4.
Its12 paired get calls record:

| Policy CPU | Domain CPU mask | OPP count | Mode / FDVFS | Raw readback kHz |
|---|---|---|---|---|
| 0 | F | 18 | kHz / off | 480000 in all four samples |
| 4 | 70 | 30 | kHz / off | 2446000,1250000,1328000,1302000 |
| 7 | 80 | 31 | kHz / off | 400000 in all four samples |

Every raw read exactly matches its corresponding cpuinfo_cur_freq output.
This closes the meaning of that sysfs file for this active provider: it
reflects the shared performance word, not an independent clock-edge count.
The samples were read while the normal Android governor remained active.
They do not distinguish an OS-written request from any later firmware update
of the same word, and do not prove the physical clock.

CPU4's four values are outside its exported discrete OPP list. Therefore the
earlier blanket statement that Linux writes/reports only resolved LUT values
is withdrawn: the compiled fast-switch path preserves its supplied kHz value,
and intermediate kHz readback now has direct hardware evidence. This makes a
continuous request interface more plausible. It still does not prove safe
behavior for every CPPC input, clamping, completion, thermal enforcement, or
availability after UEFI handoff.

`audit_active_cpufreq.py` validates the loaded note, exact hash, offsets and
register layout, and executes the original `.get` instructions in Unicorn.
Nine host cases pass, including all four intermediate values. Exactly one
modeled32-bit request read occurs per call and no modeled MMIO write occurs.
External read-logging calls are modeled; only the two .get BSS relocations
are applied. `active-cpufreq-audit.json` contains the host cases and parsed
capture. This does not run the target, initialization or fast-switch routines
against hardware.

### Minimal observation points; no guessed physical mapping

Two temporary root probes captured the existing read without another SRAM access.

- `mediatek_cpufreq_hw:mtk_cpufreq_hw_get+0x24`, at .textC28, precedes
  `LDR X0,[X20,#18]`. X0 is the policy CPU, X20 is the domain and X8 is the
  relocated .bss+8 CPU-map base. Fetch `cpu=%x0:u32`,
  `mode_flag=+272(%x8):u8`, `fdvfs_flag=+276(%x8):u8`.
- `mediatek_cpufreq_hw:mtk_cpufreq_hw_get+0x30`, at .textC34, precedes
  `LDR X8,[X20]`. Fetch `raw=%x0:x32`, `domain=%x20:x64`,
  `req_va=+24(%x20):x64`, `lut_va=+8(%x20):x64`,
  `ready_va=+32(%x20):x64`, `latency_va=+48(%x20):x64`,
  `nr_opp=+56(%x20):u32`, `cpus=+64(%x20):x64`.

The pointer and metadata fetches read ordinary module/domain RAM; X0 supplies
the SRAM value already read by the driver. The event's executing CPU is not
necessarily the target policy CPU. Pair events by task and domain. The ELF
function has size88; live kallsyms reports the next-symbol span8C, which does
not change the verified entry or offsets.

For a separately bounded extension, `ready=+0(+32(%x20)):x32` and
`latency=+0(+48(%x20)):x32` add reads at pointers this exact driver already
reads during initialization. This is source/binary-qualified, but it must be
labeled as two new SRAM reads rather than reused read results. Prefer these
explicit mapped pointers to arithmetic outside the mapped120-byte resource.
The enable pointer is at domain+10 and only write use was established in the
active functions; omit its extra read from the minimal extension. The global
CSRAM pointer can be stale after the driver's iounmap and must not be reused.
The exported scaling_available_frequencies already supplies the decoded LUT;
raw LUT words and a mode parameter are not exported by this module.

The alternative FDVFS path requires its own DT/resources/magic and encodes
ceil(kHz/26000) in per-CPU request slots, with a separate CCI request. The
captured DT has a GPU fdvfs node; it does not establish this CPU FDVFS path.
[Pinned FDVFS implementation](https://github.com/MiCode/MTK_kernel_device_modules/blob/3c4ccd07d5624879555ec710c11bb8818e5cb5c6/drivers/cpufreq/mediatek-cpufreq-hw_fdvfs.c)

The retained LK log at
`evidence/raw/2026-09-14-memory-display/pl_lk.txt:7065` reserves the1MiB
MCUPM region at7FD00000. Lines7069..7082 authenticate/load
tinysys-mcupm-RV33_A from mcupm_b. This supports a resident-firmware route
without replacing early firmware; it does not prove the required service is
initialized or remains ready during Windows.

The public MCUPM driver separately initializes mailbox/IPI service, reserved
shared memory and platform/time services. A different MT6873 cpufreq source
contains DVFS/EEM initialization IPIs; its chip-specific commands are not an
MT6897 recipe. [Pinned MCUPM service driver](https://github.com/MiCode/MTK_kernel_device_modules/blob/3c4ccd07d5624879555ec710c11bb8818e5cb5c6/drivers/misc/mediatek/mcupm/v2/mcupm_driver.c)

The same-SoC thermal interface includes per-cluster ceiling information,
CPU/DSU temperatures, active masks and policy hints, backed by thermal shared
memory/TCM. Consequently the frequency request word alone is not the complete
thermal contract. Firmware enforcement that survives OS replacement must be
distinguished from Android software policy that is absent under Windows.
[Pinned thermal interface](https://github.com/MiCode/MTK_kernel_device_modules/blob/3c4ccd07d5624879555ec710c11bb8818e5cb5c6/drivers/thermal/mediatek/thermal_interface.c)

## Read-only CSRAM observation contract

Captured DT rows159..162 and410..412 in
`evidence/raw/2026-10-25-degas-ufs-baseline-v2/live-fdt.tsv` supply these
resources. The conservative initial allowlist is:

| Observation | Physical address/range | Interpretation |
|---|---|---|
| Domain0 resource | 11BC10..11BD2F | CPU0..3 |
| Domain1 resource | 11BD30..11BE4F | CPU4..6 |
| Domain2 resource | 11BE50..11BF6F | CPU7 |
| Per-domain LUT | base+00..7C,32 aligned DWORDs | Available OPP encoding |
| Per-domain request | base+88 | Requested index/kHz, not measured clock |
| Per-domain ready | base+8C | CPUFREQ bit0, SVS bit1; not clock frequency |
| Per-domain latency | base+114 | Reported transition latency, not measured completion |
| Global request mode | 11C0CC | CSRAM11BC00+4CC, inside resource11BC00/1400 |

The enable word base+84 is a firmware handshake/control location, not a
frequency meter. Source shows writes there; include a read only after its
shared-SRAM read semantics are explicitly accepted. No enable/reset write,
panic-log-stop write, QoS/CCI request, mailbox initialization, or whole-address
space sweep belongs in this observer.

Map only the source-backed resources with Device/uncached semantics, qualify
current EL1 translation/read access without changing attributes, perform
aligned32-bit reads, retain raw words and build identity, and restore no
values because no control value was changed. Any failed qualification stops
before access. Repeated samples can reveal autonomous firmware changes.
They cannot establish physical frequency. A UEFI snapshot must retain its
phase label and cannot substitute for a Windows runtime sample.

## Independent runtime frequency measurement

The useful physical evidence is a per-CPU cycle-counter delta against a
qualified invariant time delta during a controlled active interval. Pin the
sampler, record processor number and MPIDR before/after, retain raw counters,
and reject migration, zero/nonmonotonic counters, unknown divider/filtering
or trapping/virtualization behavior. Do not enable or reset PMU/AMU counters
in the first observation phase. Native NT already initializes a PMCCNTR path;
its actual state and access still require read-only qualification.

AMU is an architectural feedback candidate, but there is a concrete obstacle
on this phone. The retained AP probe reports CPU0..3 MIDR411FD461, A510r1p1,
and CPU4..7 MIDR411FD4D0, A715r1p0. Linux's2457168 workaround covers
A510r0p0..r1p1 and suppresses the affected constant-counter CPPC read. Therefore
an unqualified AMU core/constant ratio is unacceptable on the little cores.
[Linux erratum table](https://code.googlesource.com/linux/torvalds/linux/+/058f2c4b7533365d99d4868fb29a68a5b62ecf5a/arch/arm64/kernel/cpu_errata.c),
[Linux AMU/CPPC handling](https://code.googlesource.com/linux/torvalds/linux/+/058f2c4b7533365d99d4868fb29a68a5b62ecf5a/arch/arm64/kernel/topology.c)

An already-enabled valid core counter versus CNTVCT/CNTPCT can instead report
active-cycle rate over wall time, with the idle/EL2-time limitation labeled.
The invariant timer frequency13MHz is a timebase, not a CPU clock. A complete
measurement design must account for core gating, PMU filters/division, counter
ownership and GZ interception. The vendor clock-fmeter helper is not a
read-only substitute: it writes test-selection, calibration and meter control
registers. No fmeter invocation or raw PLL operation is proposed.

## `_CPC` direct mapping versus Windows PEP

ACPI CPPC uses an abstract continuous performance scale. OSPM may request
intermediate values and the platform must map them to internal states. It
allows SystemMemory registers and optional absent controls; these standards
facts do not prove support by this exact Windows ARM64 processor provider.
[ACPI6.5 processor control](https://uefi.org/specs/ACPI/6.5/08_Processor_Configuration_and_Control.html)

Direct DesiredPerformance→base+88 needs all of the following established:

1. The encoding after UEFI handoff remains fixed kHz, not reverse-index or
   FDVFS. Android's cached flags now establish its own kHz path.
2. Resident firmware accepts the complete proposed CPPC request range and
   handles boundaries/clamping/completion safely. Android's observed
   intermediate values are useful evidence, but not a complete contract.
   In index mode, index0 is fastest while CPPC values
   increase with performance; a GAS register descriptor cannot invert a LUT.
3. Actual Windows ARM64 `_CPC` parsing, flexible-address-space `_OSC`
   negotiation and32-bit SystemMemory access are qualified with the selected
   processor driver. No primary evidence establishing this specific path was
   found in the bounded check. This is an open prerequisite, not a claim that
   Windows universally lacks CPPC.
4. Shared-domain coordination and register ownership cover the4/3/1 topology.
   Do not expose one independently controlled request per core when hardware
   accepts a shared-domain request. Correct full-width accesses avoid partial
   field read/modify/write races.
5. Highest, nominal, lowest and nonlinear performance thresholds are honest;
   do not invent a sustained nominal clock or efficiency knee from advertised
   maxima. Required feedback and thermal-limit semantics must also be defined.

The more explicit Windows integration candidate is a platform PEP backend
that translates OS performance requests into validated discrete LUT states,
serializes each domain, reports feedback and current constraints, and preserves
resident voltage/clock sequencing. Microsoft's capabilities interface carries
DomainId/DomainMembers and platform-specific performance units.
[PEP performance capabilities](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/pep_x/ns-pep_x-_pep_ppm_query_perf_capabilities)

The domain interface describes coordination, affinity requirements and
transition latency. These must reflect controller behavior rather than values
selected to make a UI look fast. [PEP domain information](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/pepfx/ns-pepfx-_pep_ppm_query_domain_info)

PEP exposes performance-set, constraint and feedback notifications, with
runtime IRQL/affinity constraints. A blocking mailbox handshake cannot simply
be placed in a DISPATCH_LEVEL performance callback. Feedback, autonomous
operation and idle support should only be advertised when implemented;
unrelated idle/resume control must not be introduced while adding DVFS.
[Microsoft PPM notifications](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/ppm-notifications)

## Implementation order and go/no-go boundary

Android module identity and request mode are now pinned. Next retain
controller/PMU identities and read-only UEFI snapshots. Then use
a separately reviewed Windows telemetry component to compare request words,
qualified cycle-rate measurements and OS reports on all three domains. This
distinguishes stale reporting, an already active resident governor and a truly
fixed request. It preserves the known SMP bridge unchanged.

Before any control implementation, prove the exact MT6897 firmware service,
request units, valid OPP set, completion semantics, domain synchronization,
SVS/voltage ownership, independent thermal/overcurrent limits and failure
fallback. Preserve the existing MCUPM reservation and firmware. A missing
service/readiness bit is not permission to write enable1 or replay an older
chip's initialization command. The eventual backend may use firmware-owned
performance requests without direct voltage/PLL programming, but that is a
qualified later operation, not today's change.

No `_CPC` declaration targeting real control memory, frequency-request driver,
fixed-high-OPP workaround, clock/power-register write, early-firmware change or
SMP-bridge change was produced. The host audit and this report are reviewable
offline evidence; they do not claim that Windows DVFS is fixed.
