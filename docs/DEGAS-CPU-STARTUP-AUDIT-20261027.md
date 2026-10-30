# CPU startup and frequency follow-up

Hardware outcome: user observed one logical processor in full Windows, with
displayed speed moving between approximately 900, 1000 and 1300 MHz. This
supersedes any interpretation of the preboot 8/8 counter as OS SMP success.
Fresh retained console from the subsequent abrupt reset was unavailable.
The debuglogger copy is the preceding menu-only log, not the crashed boot.
Saved System.evtx includes another CPU0-only processor-power record (522).
Kernel-Power without a bugcheck code cannot identify the reset mechanism.

## ACPI correction

The degas MADT generator assigned UID to both CPU Interface Number and ACPI
Processor UID. The working OP12 generator assigns zero to the former and
UID to the latter. ACPI GICC specifies zero for CPU Interface Number on
GICv3/4 without GICv2 compatibility. The degas description exposes system
register GICv3, with no GICv2 CPU interface base; changed the former field
to zero. MPIDRs, GICR addresses, UIDs, enabled flags and efficiency classes
are unchanged. This is a description correction, not a demonstrated root
cause of the one-CPU result.

Spec: https://uefi.org/specs/ACPI/6.5/05_ACPI_Software_Programming_Model.html
section 5.2.12.14. Four independent ACPI generator tests pass, including
the corrected zero/UID/flags triple in every GICC entry.

## Execution probe, menu-only

Replaced RAM ALLOCATION TEST with CPU STARTUP TEST in a diagnostic image.
No AP is started automatically. Requires BSP affinity zero, advertised
CPU_ON64 and CPU_OFF support, and all seven target affinities initially OFF.
Code and mailboxes are separately allocated below 4 GiB; no hard-coded
free-memory address. Code is position independent, 96 bytes, no stack,
no C calls, no relocations. Actual COFF object disassembly checked.

For each MPIDR 100..700, CPU_ON64 receives the physical copied code address
and an individual 128-byte-aligned mailbox as context_id. AP masks interrupts,
stores MPIDR/MIDR/CurrentEL/CNTFRQ, publishes completion with DSB, and invokes
CPU_OFF. BSP invalidates only clean mailbox cache lines when polling and
checks AFFINITY_INFO returns OFF before continuing. Timeouts bound each
mailbox and OFF wait to 500 ms. Watchdog protects SMC/early-execution hangs.
All buffers remain allocated, including on failure. Function exits to the
mandatory reset path; Windows cannot be selected after executing this test.
No clock, voltage, GIC interrupt-routing or storage registers are programmed
by the probe (apart from existing watchdog reload/arming).

Success establishes PSCI AP entry and shutdown from UEFI, not Windows AP
initialization, interrupt delivery, timer operation or DVFS. Failure retains
CPU_ON return code, mailbox fields and final affinity state where available.

Build passed 21:07:33 Moscow. Image:
`artifacts/degas-cpu-startup-v1/boot.img`, SHA256
`0db2be0bec41136ab8dbed72fabebb0c4e978c6e54c426d15a15d6e15a54cab3`.
Stock AVB tail preserved. NOT FLASHED: user took phone and requested offline
work. Hardware test pending.

## Frequency findings and limits

Our SMBIOS supplies no 1300-MHz constant; max/current/external clock fields
are zero. Saved Kernel-Processor-Power events report NominalFrequency=1300,
PerformanceImplementation=0. These are OS reports, not physical clock
measurements. User's changing Task Manager number does not by itself prove
Windows controls DVFS or disprove the absence of our PPM implementation.

Captured Android topology has three performance domains: CPU0-3 480..2200MHz,
CPU4-6 400..3200MHz, CPU7 400..3350MHz. Captured DT maps the domains to
0x11bc10/0x120, 0x11bd30/0x120, 0x11be50/0x120, with performance-domains
phandle2 indexes0,1,2. cpuhvfs CSRAM resource is0x11bc00/0x1400.

Inspected official MiCode device-modules commit
3c4ccd07d5624879555ec710c11bb8818e5cb5c6, four files copied to
artifacts/degas/cpu-audit. Main driver has LUT low12 bits in MHz, +0x84
enable, +0x88 requested performance state, +0x8c hardware status. Its
target path can write OPP index OR frequency in kHz, depending on
CSRAM+0x4cc mode; a third FDVFS path uses per-CPU requests divided by26000.
Source presence does not prove the installed binary uses the same branch.
No CPU `fdvfs` node found in the captured DT (GPU fdvfs is unrelated).
Do not write a guessed index/voltage/PLL setting. Requested-state decoding
would still not constitute an independent physical frequency measurement.

Runtime logger now also queries CallNtPowerInformation(ProcessorInformation)
and labels max/current/limit values explicitly OS_reported. Microsoft defines
CurrentMhz via maximum specified frequency times current throttle, so it is
not our independent hardware clock measurement:
https://learn.microsoft.com/en-us/windows/win32/power/processor-power-information-str
Logger ARM64 build passes; deployment remains pending safe NTFS access.

Next physical action: explicit CPU STARTUP TEST, read seven actual AP records
after automatic reset. If 7/7, next Windows boot tests corrected GICC field;
OS active-count capture is still required before claiming SMP fixed.

## First actual probe result and instrumentation correction

User reported 0/7. Fresh retained log captured at
evidence/raw/2026-10-27-degas-cpu-startup-v1-result-retry (initial collector
attempt raced ADB enumeration and failed; retry uses C:/adb first in PATH).
CPU1 CPU_ON returned0, but all mailbox fields remained0 and final PSCI
affinity state wasOFF. Entry43cac000, mailbox43cab000, BSP CurrentEL4.
This does not distinguish failed entry from invisible stores or bad context
delivery. Do not claim PSCI broken from this result alone.

V2 explicitly maps the private mailbox page UC|XP through CpuArch protocol,
stops if that fails, and stores flag0x10 before reading MPIDR/MIDR/CNTFRQ.
AP obtains its mailbox from a patched PC-relative literal, independently
records original context_id, and final pass requires exact context match.
Each literal update is cleaned to PoC before CPU_ON. Actual COFF code has
no relocations, size120, literal offset112; source/header timestamps precede
both object timestamps. Continues testing further affinities only if the
previous target is confirmed OFF, even if its record was missing. No
Windows boot is permitted after the test returns. Firmware build passed
21:25:45 Moscow; image artifacts/degas-cpu-startup-v2/boot.img SHA256
6eed24bc3414b5dfaeb90e8d70c809dac5f64dfd80c05fc39bc1ef28657e06ca.

## V2 result: all APs execute; incoming context is zero

Fresh evidence/raw/2026-10-27-degas-cpu-startup-v2-result retained console:
mailbox UC mapping succeeded; all seven CPU_ON returned0; every record flag1,
MPIDR81000100..81000700, correct A510/A715 MIDR, CurrentEL4, CNTFRQ13000000,
final stateOFF. All seven recorded incoming x0/context=0 although caller
requested the nonzero per-CPU mailbox physical address. Independent literal
made records available despite this. Hence reported0/7 is the strict ABI
pass count, NOT zero executed APs. The on-screen label is misleading and
must be clarified in the next firmware revision.

The actual installed Windows kernel was copied read-only to cpu-audit.
Disassembly finds a CPU_ON64 wrapper at imageVA1404a8da8: function loads
C4000003 into w0, constructs MPIDR in x1, and preserves caller x2/x3 through
SMC dispatch. That alone does NOT establish a particular startup caller
passes nonzero context or that context loss caused this Windows failure.
No Windows binaries were patched. Next isolate the already-corrected GICC
CPU Interface Number through an ordinary Windows boot before developing
any fallback startup mechanism. Legacy parking protocol is not a drop-in
replacement for compliant PSCI; Microsoft requires PSCI when supported.
