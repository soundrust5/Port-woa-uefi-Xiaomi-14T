# Degas Windows performance provider contract — 2026-10-29

Choose a performance-only Windows PEP backed by the existing MediaTek CSRAM
request interface. Keep native PSCI CPU startup and the working eight-CPU ACPI
enumeration. The performance provider now implements DriverEntry, secure
read-only telemetry, exact processor-device registration, PoFx registration,
performance capabilities, shared-domain SW_ANY coordination, constraints and
PERF_SET. Hardware and thermal access live behind `degas_backend.h` and are
implemented separately. This is host-side work, not a demonstrated Windows
hardware result.

`\\.\DegasDvfs` accepts only `IOCTL_DEGAS_DVFS_SNAPSHOT` with read access,
and has an explicit SYSTEM/Administrators ACL via IoCreateDeviceSecure. The
version3 fixed 556-byte result includes backend initialization and PEP
registration status, accepted CPU mask, three capability sets, request/ceiling,
readiness, thermal status/validity and latched faults. It exposes no control
IOCTL or arbitrary register operation. Failed qualification keeps this device
available in observe-only mode and does not register a PEP.

The version2 suffix begins at offset224 and contains RawThermalValid, eight
raw core temperatures in milli-Celsius, three raw ceiling values in kHz,
RawUtcCount and FreshnessQualified. RawThermalValid means a bounded raw read
succeeded; it does not authorize a performance transition. These fields permit
comparing Windows samples for liveness evidence. FreshnessQualified remains
FALSE until the runtime guard qualifies freshness, and effective per-domain
ceilings remain at their lowest validated states while qualification is absent.

Version3 preserves that entire 280-byte prefix and appends four DWORD counters
(DpmPrepareCount, DpmRegisterCount, PpmNotificationCount, PerfSetCount), a bounded
128-WCHAR LastRejectedDeviceId and DWORD LastAcceptedUid (MAXULONG means none).
Unknown CPU-looking names are retained for the first actual PEP registration
probe; unknown identities remain rejected. This makes naming/load-order failures
visible without a kernel debugger or another speculative reboot.

The observer-v2 retry now supplies Windows liveness evidence:
`evidence/raw/2026-10-29-degas-windows-dvfs-observer-v2-retry/thermal-liveness.json`
records 120 samples over about145 seconds, changing temperatures on all eight
cores and stock raw ceilings. This supersedes the earlier absence of Windows
thermal-producer evidence. Liveness alone does not establish an independent
firmware thermal governor. The next provider revision permits early registration
after controller validation while keeping boost dependent on the runtime guard.

The tested observer-v2 thermal adapter deliberately returned FALSE from its
transition guard, reported `STATUS_DEVICE_NOT_READY`, and never registered PEP.
Its stock 2.2/3.2/3.35GHz values were capability metadata, not Windows transition
evidence. Activation work adds runtime freshness and conservative software
thermal caps; successful compilation does not establish that Windows has
consumed PEP performance notifications or executed those transitions.

## Evidence and limits of the choice

The clock-step-v2 retry evidence establishes a real little-domain transition
under UEFI: requests 1300000 → 1400000 → 2000000 → 1300000 kHz produce approximately
1.300 → 1.376 → 1.976 → 1.300 GHz by independent PMU measurement. Enable is 1,
ready is 3, and global request mode is 1. This removes the earlier assumption
that a control path is entirely unproved. It does not establish Windows EL1
access, the other two domains, all OPPs, sustained thermal behavior, or restart
and suspend behavior.

The exact local WDK is
`artifacts/toolchains/wdk-arm64-10.0.26100.6584/c/Include/10.0.26100.0/km/pep_x.h`.
It supplies PPM capability/domain/performance callbacks, explicit member counts,
SW_ANY coordination, and performance units chosen by the platform. The active
NT capture and `artifacts/degas/cpu-audit/ntoskrnl.exe` have matching SHA256
`5bb49f8383e38b26ef0c18f3087db3be541e99143b80b92d2f83d824b7e5301c`.
The local disassembly contains exported `PoFxRegisterPluginEx` at image RVA
0x51ccb0 and `PoFxRegisterPlugin` at RVA 0x6e4d40. This establishes the registration
entry points, not successful platform ownership at runtime.

The matching public-symbol search is retained in
`diagnostic/degas-dvfs-windows/kernel-provider-symbols.txt`. It finds hypervisor
CPPC names and PoFx PEP performance helpers. Those names do not establish a
native ARM64 `_CPC` SystemMemory provider. The bounded local evidence contains
NT and ACPI binaries, but no independently identified active processor-provider
binary and its registration/configuration proving that route. Therefore this
report does **not** assert that Windows universally lacks `_CPC`, or that PEP
is architecturally mandatory. Direct `_CPC` is not a qualified shortcut for this
installed Windows image. Its missing proof is an actual provider consuming
32-bit SystemMemory GAS requests on ARM64 with the 4/3/1 shared-domain topology.

## Exact provider model

| Domain ID | Physical/logical CPU UIDs | Member count | CPU mask | Resource base | Request address |
|---|---|---:|---:|---:|---:|
| 0 | 0..3 | 4 | 0x0f | 0x0011bc10 | 0x0011bc98 |
| 1 | 4..6 | 3 | 0x70 | 0x0011bd30 | 0x0011bdb8 |
| 2 | 7 | 1 | 0x80 | 0x0011be50 | 0x0011bed8 |

Each resource is 0x120 bytes; LUT is 32 DWORDs at +0, frequency bits 11:0
are MHz; enable is +0x84, ready +0x8c and transition latency +0x114.
Global mode is the separate DWORD 0x0011c0cc. Do not confuse `DomainMembers`
with a bitmask: it is 4, 3, or 1. The masks above are private lookup metadata.
Bind handles using verified ACPI UID/processor-number/MPIDR identities at
registration; do not equate a callback's executing CPU with its target handle.

Use `PEP_NOTIFY_PPM_PERF_SET` first, in kHz platform performance units.
Do not advertise discrete-state notifications, autonomous operation, EPP,
scheduler-directed transitions, parking or new idle states in the first
implementation. The backend validates the actual LUT, units and safe envelope.
The provider sends a continuous kHz value, matching the demonstrated controller
mode; the backend owns any hardware quantization. A raw LUT row index is never
sent while global mode is 1.

| Notification / phase | Required behavior |
|---|---|
| DPM prepare/register | Accept only exact root processor paths `\_SB.C000`..`C007` or padded NameSeg form `\_SB_.C000`..`C007`; retain kernel and PEP handles. Decline unknown and duplicate devices. Log declined identities for qualification. |
| QUERY_CAPABILITIES | Enable performance support only after all qualification gates below pass. Other capabilities remain absent; no new PSCI/idle handler. |
| QUERY_PERF_CAPABILITIES | Return per-domain highest, nominal, lowest nonlinear and lowest in kHz; domain ID and member count from the table. Highest is the qualified ceiling, not automatically the advertised silicon maximum. |
| QUERY_DOMAIN_INFO | Request `PROCESSOR_DOMAIN_COORDINATION_SW_ANY`; one hardware request controls the domain. Set AffinitizePerfSet FALSE, SchedulerDirectedTransitionsSupported FALSE. Hardware does not aggregate separate per-core slots, so HW_ALL is inappropriate. |
| PERF_CONSTRAINTS | Return the current qualified ceiling and actual limiting reasons. On changes, use the documented constraint-change notification after the processor device has started. |
| PERF_SET | Nonpageable, bounded, DISPATCH_LEVEL path; select an allowed state, serialize that domain, and in the future perform one aligned 32-bit request write. Retain raw input, selected state and completion evidence. Never block on a mailbox or poll indefinitely. |
| Feedback | Advertise zero counters until a qualified implementation exists. Never label request-word readback as measured delivered performance. |
| System state changes | No idle/resume ownership is claimed. Backend validity/readiness checks must reject requests when resources become invalid; a failure latches domain control off until reboot. |

SW_ANY makes Windows coordinate the domain rather than asking each core to
write its own competing request. Maintain one resident lock and cached request
per domain regardless; do not aggregate an additional maximum across stale
per-core requests. A shared SRAM write does not need target-core affinity once
Windows access semantics have been qualified.

When a backend safety worker changes an effective ceiling or its reasons, it
calls `DegasPepNotifyConstraints(domain)` after releasing backend locks. The
provider marks each started processor in that domain pending, invokes PoFx's
RequestWorker, and answers DPM_WORK with a static per-CPU power-control record
using GUID_PPM_PERF_CONSTRAINT_CHANGE and no buffers. Records are not reused
until POWER_CONTROL_COMPLETE. Repeated changes coalesce while a record is in
flight. No transaction is sent before DEVICE_STARTED. Initial transactions
are queued after successful PEP registration. This asks Windows to requery
PERF_CONSTRAINTS; the backend must independently enforce emergency reductions
even if OS work or completion is delayed.
[Microsoft PPM power control codes](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/ppm-power-control-codes)

Hardware management starts separately for each domain only after its first
successful OS PERF_SET write. Before that, the worker reports changing limits
but leaves the boot request untouched, including when PEP registration or CPU
attachment fails. A first request received while the guard is closed is accepted
at the safe low cap and establishes management. Thereafter stale/hot worker
passes independently lower managed domains; freshness recovery never raises
their requests without another OS PERF_SET.

For `PERF_SET`, intersect the OS minimum/maximum with validated static domain
limits. Clamp desired kHz to the resulting
continuous interval of OS and static hardware bounds. Pass desired, minimum and
maximum to the backend so it can select a continuous kHz request within those
limits, consistent with the active Android fast-switch ABI. Physical clock
quantization remains distinct from request encoding. An external thermal cap
may force a request below the OS minimum;
accept the safe lowered transition and report PERFORMANCE_LIMIT_THERMAL rather
than reject the emergency reduction and leave a high request in place.
Malformed static intervals are rejected without permanently disabling future
valid requests. Backend controller/access failures still latch the domain off.
The backend performs final limit and readiness checks before its aligned write.
`PerformanceTolerance` is a permitted lower bound,
not a frequency to add to the desired value. A nonzero `TimeWindow` permits
average behavior; immediate valid desired-state selection remains the initial
policy. No permanent 2GHz request is part of this provider.

## Qualification needed before admitting hardware requests

At PASSIVE_LEVEL, identify the active Windows processor/PEP owner and record
accepted DPM/PPM notifications. Establish that only one performance provider
owns these eight processors. Registration cannot be undone without a restart.
The provider registers after controller/backend initialization and limit
validation succeed, retains no unload callback, and remains observe-only on
structural initialization failure. A not-yet-fresh thermal guard keeps requests
and reported ceilings safe without preventing early processor registration.
A boot-start service must register before processor initialization. The exact
root paths are derived from the generated DSDT; actual Windows DPM identities
must still be captured to confirm its naming convention and load order.

Qualify Windows runtime mapping with the correct Device/uncached attributes and
aligned reads of only these resources. Require enable=1, ready bits0/1 set,
and mode=1. Initial read-only runtime traces must demonstrate stable identity
and readiness. Initialization uses only the source-backed enable=1 handshake
at PASSIVE_LEVEL; mode, readiness, voltage, PLL, mailbox, CCI/QoS and firmware
initialization commands remain untouched.

Next qualify each domain's permitted OPP envelope, latency and completion
semantics, including the two domains not stepped in the cited UEFI experiment.
The controller latency word's units and applicability must be established;
Windows domain latency/overhead are in 100ns units. Never copy the raw word
into those fields without conversion and measurement. Nominal and nonlinear
thresholds require sustained/efficiency evidence; neither boot 1300MHz nor
silicon maximum establishes them. Before enabling dynamic control, establish
which thermal/overcurrent ceiling is resident-firmware enforced and how Windows
learns changes. Software-only Android policy cannot supply Windows protection.

The eventual request path must stop admitting new requests on mode/readiness
loss, retain the last qualified request, and emit an actionable diagnostic.
Do not promise a successful write/transition after a fault or advertise a
counter that is merely the cached target. Feedback can be added later using
qualified counters; A510 erratum 2457168 prevents unqualified AMU ratios on
CPU0..3. PMU active-cycle rate must retain its idle/filter/divider limitations.

## Offline artifact and verification

Run `wsl --exec bash diagnostic/degas-dvfs-windows/build_driver.sh` from the
repository root. It tests the same portable identity/domain/bounds protocol
used by the provider, compiles provider and backend with the actual local ARM64
WDK, and links `artifacts/degas/dvfs-windows/degasdvfs.sys`. The provider objects
compile with `-Wall -Wextra -Werror`; host tests cover all eight identities,
unknown paths, 4/3/1 topology, requested bounds, thermal ceiling, stock maxima
and invalid intervals. Full ARM64 driver linking passed on 2026-10-29.
`test_backend.sh` now routes to the activation backend tests. The actual backend
C executes against isolated fake SRAM with a deterministic mock worker: exactly
three enable writes at initialization, no frequency writes before worker start,
zero frequency writes over closed/fresh/stale worker passes before any accepted
OS request, first-Set ownership at a safe low cap, continuous OS bounds and all
three stock maxima once fresh, thermal
changes injected at domain-lock acquisition, emergency minimum override,
mode/readiness loss, notification outside locks and joined shutdown before
unmapping. All cases pass. The older closed-init test and verification-v2.json
describe the historical observer only. Mock tests do not exercise Windows SEH,
real scheduling, CPU interrupts or the hardware controller. The shim is never
on the driver build's include path. A separate `build_contract.sh` retains the original
nonregistering ABI probe for reference. Neither compilation nor host tests
establishes successful Windows PEP ownership or physical clock transitions.
No phone command, firmware edit, registration, installation, reboot, or hardware
write was performed by this work.

The 200ms PASSIVE safety worker has no hard execution deadline under scheduler,
DPC or whole-OS stalls. Cached freshness expiry prevents the next raise, and the
next worker pass lowers stale/hot requests, but a stopped OS cannot execute
that reduction. This software guard does not establish a resident firmware
thermal-governor guarantee.

Primary API sources: [PPM notifications](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/ppm-notifications),
[domain coordination and latency](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/pepfx/ns-pepfx-_pep_ppm_query_domain_info),
[performance request fields](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/pep_x/ns-pep_x-_pep_ppm_perf_set),
and [PEP registration lifecycle](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/pep_x/nf-pep_x-pofxregisterplugin).
