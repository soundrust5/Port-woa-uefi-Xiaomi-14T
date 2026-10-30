# Domain0 clock step: offline contract

This directory contains an executable **host-only specification**, not firmware
or a phone tool. `model.py` cannot access registers or execute its symbolic
command plan. `python test_model.py` passes five tests covering rejected gates,
inherited divider semantics, 64-bit wrap and exact restoration value.

The intended bounded test uses domain0 only: enable1, poll readiness3, request
1300000kHz, active measurement, request1400000kHz, active measurement, restore
1300000kHz, verify restoration, return to the same menu for BootAndroid.
1400MHz is the immediate next captured stock OPP above1300 and matches the
previous captured Android little-policy ceiling. No domain1/2 control write,
IPI, PLL, voltage, GZ, PMU configuration or early-firmware change is part of it.

## Read-only gate suitable for first integration

Before **any** enable/request write:

1. Snapshot and journal boot controller mode, raw domain0 LUT/enable/request/
   readiness/latency, identity and phase. Require the current expected tuple
   mode1/enable0x101/request9/ready3 and decoded original1300MHz. Requalify
   mappings for writes separately from the existing read-only AT check.
2. Confirm execution on the DT-qualified domain0 PE (prefer BSP CPU0), and
   current EL1. Read `ID_AA64DFR0_EL1` PMU version, `PMCR_EL0`,
   `PMCNTENSET_EL0`, `PMCCFILTR_EL0`, `PMCCNTR_EL0`, `MPIDR_EL1`,
   `CNTFRQ_EL0` and the selected architectural reference counter. Use
   individually guarded reads; log the raw values without normalization.
   An inaccessible/disabled/filtered/unqualified PMU yields INCONCLUSIVE and
   **zero frequency/enable writes**. Do not enable a disabled cycle counter.
3. Narrow initial supported configuration: PMCR.E1, PMCNTENSET.C1,
   PMCR.LC1, PMCR.DP0, PMCR.FZO0, PMCCFILTR0, stable EL/PE/metadata.
   DP1 is not intrinsically broken; this narrow model declines its extra
   prohibited-counting context. Filter0 is qualified here only for nonsecure
   EL1. Do not generalize its meaning to EL2. Log the implementation version
   and reject unknown semantics rather than guessing width or scale.
4. Perform three active busy-loop windows of10ms, with no WFI/WFE/Stall,
   screen update or serial logging inside the window. Serialize reads with
   ISB as appropriate and bracket the cycle reads with reference timestamps
   so acquisition uncertainty can be bounded. Reject an excessive bracket
   spread, PE changes or changed metadata, non-advancing counter, anomalous
   wrap, inconsistent samples or baseline outside5percent of1300MHz.
   A stable result is evidence of the observed counter rate; unknown
   hypervisor mediation remains a limit on claims about physical cycles.
5. Require fresh, explicitly recorded Android little-policy ceiling at least
   1400MHz and current temperature assessment before authorizing a later
   actuation build. `thermal_gate` represents that explicit review; the host
   tests' mock string is not valid hardware evidence. There is no qualified
   live UEFI thermal sensor in this design. A stock OPP and short duration
   alone do not establish a runtime temperature limit or firmware thermal
   protection. Do not invent a raw sensor address or threshold.

PMCR.LC controls overflow semantics as well as interacting with D: with LC1,
D is ignored and cycle count is not multiplied by64. With LC0/D1 a naive
every-cycle conversion is wrong; this narrow first gate declines LC0 entirely.
Never save and then write back PMCR as if it were plain memory: its reset bits
P/C are commands and other fields affect inherited state. This test reads PMU
state only, so no PMU restoration writes are needed. Include DP/FZO and filter
metadata because an enabled counter can still be prohibited or frozen.

These rules follow the [Arm PMU initialization comments](https://android.googlesource.com/platform/external/arm-trusted-firmware/+/7606f524cc2d692c47ad85e6951ebfedd57b1a1b/include/arch/aarch64/el3_common_macros.S),
[Arm PMU freeze clarification](https://documentation-service.arm.com/static/644c18cbaa78c007af74fa2c?token=),
and the [A78 PMCR field reference](https://docs.amd.com/r/en-US/am028-versal-gen2-register-reference/PMCR_EL0-DBG_A78_PMU-Register?contentId=szPrW9smmpgrxzkitiotzg).
The field reference is not evidence for the phone's live PMU configuration.

## Later actuation backend contract

The original documented Linux enable1 handshake is supported by exact code and
Android v3 enable1 readback across all domains. A further early Linux trace is
not needed merely to establish that write. The remaining work is making the
small UEFI test observable and recoverable.

Use an independent monotonic architectural counter deadline, established
**before** the1400000 write. Nominal active measurement windows are30ms, with
the entire high request residency capped at2s including all settling/checks.
Do not wait two seconds deliberately. No allocations, UI, polling with an
unbounded callback, or logging after the high write before restoration.
Request readback is not a completion indication; readiness3 may remain set.
Measure the rate separately, label clamping/no change as a result and do not
escalate the request when it fails to rise.

All normal errors after the first enable write enter a single restoration path
that submits1300000kHz, orders/readbacks that request, checks ready, remeasures
the active rate and journals the outcome. Never restore index9 into the kHz
path, and do not restore boot0x101: leave documented enable1 for BootAndroid.
If restore fails, show FAILED RESTORE and do not report success or silently
continue another clock experiment. This intentionally restores the original
decoded frequency, not the exact pretest enable/request bit pattern.

## Concrete integration gaps

* The existing snapshot adapter qualifies EL1 **read** translation and uses a
  reset watchdog for an access abort. It does not provide exception-safe MRS
  recovery or write permission qualification. Raw inline MRS cannot promise
  “trap -> return to same menu”; a GZ-handled trap might not reach a UEFI
  exception handler. First ship the isolated read-only PMU gate and obtain
  its successful journal before incorporating controller writes.
* A deployed adapter must guard all sample/config reads, bound the reference
  timer, verify PE identity and prevent an unhandled fault from bypassing the
  restoration path. A reset watchdog is recovery from a hang, not proof that
  frequency restoration happened. No software2s guarantee survives an
  unhandled exception or frozen reference timer without independent support.
* Actual write mapping permissions, MMIO ordering, deadline/restoration code,
  retained journal capacity and the unchanged menu/BootAndroid route still
  need integration review and mock failure injection. The host model tests
  do not claim to test those missing mechanisms.

No shared firmware file was edited and no hardware action was performed.

## Read-only integration

The integration added `CheckpointDxe/DvfsPmuRead.S` and called it only after
the retained CSRAM record is committed. The observer reads PMU-version,
PMCR/enable/filter/reference metadata and two raw cycle/reference samples10ms
apart. It deliberately has no PMU or controller writes. It samples even a
disabled counter so disabled state can be diagnosed. Metadata is not yet
reread and PE/EL/bracket uncertainty is not yet qualified; raw values are
observations, not a passed actuation gate or physical-frequency conclusion.
On a trap or frozen reference it may require the already-armed reset watchdog;
no ordinary menu return is promised in those cases.

The actual shared assembly was assembled with:

```
wsl.exe --exec clang --target=aarch64-linux-gnu -c /mnt/d/Edk2-For-Xiaomi14T/mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/DvfsPmuRead.S -o /mnt/d/Edk2-For-Xiaomi14T/diagnostic/degas-dvfs-audit/clock-step/DvfsPmuRead.o
python diagnostic/degas-dvfs-audit/clock-step/test_assembly.py
```

Six Unicorn tests pass using this assembled code. Only MRS results and timer
progress are modeled. They cover ordinary return, no-PMU0/f, disabled counter,
trap without normal return and frozen reference without normal return.
All stores are confined to the nine-word output, and there is no MSR.

The actual `CpuDvfsSnapshot.h` host integration test was also updated in
`../uefi-snapshot/test_integration.py` with a raw PMU/status mock: six UBSan
tests pass in WSL, including prior CSRAM commit, combined journal under1800
bytes, preserved Android route/upper half, raw unavailable status and skipping
the PMU observer after CSRAM qualification failure. These are host checks and
do not emulate the phone's exception routing or PMU availability.
