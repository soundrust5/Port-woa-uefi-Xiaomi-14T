# Degas DVFS handoff and enable ordering — 2026-10-29

This is a host-side, read-only analysis of existing artifacts. No phone command,
firmware edit, controller write, PLL/voltage operation or GZ modification was
performed. It qualifies an observation plan, not a Windows DVFS implementation.

## Evidence and immediate conclusion

The UEFI-menu capture in
`evidence/raw/2026-10-29-degas-dvfs-snapshot-v1-result/decoded.json` and
`console-ramoops-0` records CSRAM base `0x0011bc00`, global `+0x4cc = 1`:

| Domain base | Enable +84 | Request +88 | Ready +8c | Latency +114 | Request interpreted as LUT index |
|---|---:|---:|---:|---:|---:|
| 0x0011bc10 | 0x101 | 9 | 3 | 1 | 1300 MHz |
| 0x0011bd30 | 0 | 20 | 3 | 1 | 1300 MHz |
| 0x0011be50 | 0 | 22 | 3 | 1 | 1200 MHz |

The final column is a decoding hypothesis, not a measured core clock. All
requests fit their respective LUTs. Global mode 1 coexists with these small
values before Linux initializes this driver. Ready 3 also coexists with enable
0, so ready bits alone cannot identify an OS-enabled controller or prove an
ownership handoff. Domain 0 enable bit 8 has no established meaning here.
The raw console's malformed printed CPU masks do not supersede the DT-derived
masks in decoded.json (0x0f, 0x70, 0x80).

Android v2 evidence
`evidence/raw/2026-10-29-degas-dvfs-requests-v2/decoded.json` has cached mode
flag 1, FDVFS flag 0, readiness 3 and latency 1 in all three domains, with
request words in kHz. Samples include 718000, 1800000 and 400000. These are
shared-memory request/readback observations, not independent clock readings.

## Exact loaded binary ordering

The analyzed `artifacts/degas/dvfs-active-modules/ramdisk00/mediatek-cpufreq-hw.ko`
has SHA256 `f12addd9a3b5c677e8fec7b11903561e2d4073dff831a5ce4bd4dc7057df51fc`,
GNU build ID `068e6f5000761002c3541edbe177a210f2299352`. Its note matches the
captured loaded-module note. Addresses below are ELF .text offsets, not live
kernel addresses. `diagnostic/degas-dvfs-audit/active-cpufreq.disasm.txt` and
`artifacts/degas/cpu-audit/mediatek-cpufreq-hw_main.c` supply binary/source
evidence; the binary resolves compiler-inverted boolean storage.

1. Probe reads global CSRAM+4cc at 258..260. If nonzero, it stores byte 1 at
   .bss+118 (274), then unmaps/releases the global mapping (278..290). This
   byte selects kHz writes in target_index and fast_switch. It does not write
   the hardware mode. Probe next calls check_fdvfs_support (294), caches its
   result at .bss+11c, builds domain mappings/LUTs and registers cpufreq.
2. Per-policy cpu_init at 888 installs domain/table pointers (900..904), reads
   latency through domain+30 (908..90c), converts microseconds to nanoseconds,
   marks fast switching possible, then calls cpu_latency_qos_add_request (938).
   Source comment describes allowing CPUs to leave idle-off for SVS startup;
   the argument is PM_QOS_DEFAULT_VALUE, not a newly derived hardware command.
3. It loads the enable pointer from domain+10 (93c), supplies **32-bit value
   1** (940), and calls the write helper (944). The helper stores W21 at
   .text+f0. This is a whole-word relaxed write, not an OR/read-modify-write.
   Blindly copying it would replace boot enable 0x101 with 1.
4. It obtains time (948), reads readiness via domain+20 (950..954), and accepts
   `(raw & 3) == 3` (958..960). The readiness helper loads W8 at ddc and uses
   `dmb oshld` at de0; do not infer that arbitrary plain loads reproduce it.
5. If not ready, it sets a 300000000 ns deadline (964..96c), sleeps with
   compiled usleep_range_state arguments **251..1000 us** (97c..988), and
   rereads until ready or the deadline is exceeded. This is the compiled
   readl_poll_timeout behavior for source POLL_USEC=1000/TIMEOUT_USEC=300000;
   it is not a fixed busy-wait at 1 ms intervals. It makes one final read on
   timeout (9ac..9bc).
6. On timeout with bit 0 clear, init returns -ENODEV after cleanup. If bit 0
   is set but bit 1 is clear it logs the SVS failure and still returns success
   (a04..a34). Normal success removes/frees the QoS request (9c0..9d0).

There is no request-word write, global-mode write, request-format conversion,
disable-before-enable sequence, explicit enable readback or ready-clear wait
in this cpu_init function. It does not establish that firmware performed an
index-to-kHz transition in response to enable. Already-set readiness would
make its first poll succeed immediately. Per-domain callback order across all
three policies is not established by this static function.

The target path subsequently writes either an index or table kHz value:
target_index a58..aa0 compares cached mode byte to 1, loads table frequency
for mode 1, then writes request via domain+18 when FDVFS is off. Fast-switch
ba0..bc0 instead writes the supplied frequency argument in mode 1; its return
value may be a resolved table frequency. `.get` c38..c78 first interprets any
raw value 0..31 as an index **without consulting mode**, otherwise accepts
in-range kHz. This proves Linux tolerates both readback encodings, including
index-valued remnants after selecting kHz writes. It does not prove the
controller consumes both formats identically in every state.

## Qualified minimum and next bounded observation

**Update from Android v3:**
`evidence/raw/2026-10-29-degas-dvfs-requests-v3/capture.txt` now additionally
reads enable through the exact domain+16 mapped pointer. All twelve samples
across three domains report enable1, ready3, latency1, cached mode1 and
FDVFS0; requests are kHz. This agrees with the exact module's whole-word
write1, including normal removal of boot domain0 bit8. No extra early boot
trace is needed merely to establish this known stock write. Bit8's separate
meaning and precise request-conversion timing remain unproved, but neither
changes the documented Linux initialization sequence.

The code-supported transition is: discover/cache global mode and FDVFS,
establish domain/LUT mappings, read latency, establish the temporary CPU QoS
context, write domain enable=1, poll readiness bits 0/1 with the bounded
timeout, then let normal cpufreq request callbacks submit kHz with FDVFS off.
It is a reconstruction of Linux software ordering, not permission to perform
these writes under UEFI or Windows.

An optional read-only experiment could observe that **existing Android boot
sequence**, capturing raw request/enable/readiness immediately before and
after its existing enable write and before/after the first existing request
write for each domain. Capture cached mode/FDVFS, domain identity, first
target arguments, timestamps and init return. Entry probes installed after
this module has initialized cannot answer the startup-order question; a
boot-time trace mechanism must already be available or separately prepared.
No module unload/reload is needed as an observation shortcut. This optional
trace is superseded as the immediate next step by the read-only UEFI PMU gate
described below.

Concrete binary observation points, for a separately reviewed boot trace:

| Location | Existing state available |
|---|---|
| cpu_init +b8 / .text940 | X22=domain; X1=enable pointer; before MOV W0,1 |
| cpu_init +c0 / .text948 | Returned from existing enable write; X22 still domain |
| cpu_init +d0 / .text958 | W0=readiness already read by driver |
| cpu_init +110 / .text998 | W0=loop readiness already read by driver |
| cpu_init +12c / .text9b4 | W0=final timeout readiness already read |
| target_index +60 / .textaa0 | W0=request value; X1=request pointer, non-FDVFS branch |
| fast_switch +108 / .textbc0 | W0=request value; X1=request pointer, non-FDVFS branch |

The domain object stores pointers at +10 enable, +18 request, +20 readiness,
+30 latency. Dereferencing them introduces new SRAM reads; register values
at the listed post-read sites reuse existing reads. These offsets are static
binary qualifications, not a deployed or execution-tested probe recipe.
The current task neither installed probes nor rebooted anything.

What remains unproved: whether requests already become kHz immediately after
enable or only after the first Linux request; who wrote the boot indices;
enable bit 8 meaning and ownership contract; live Windows readiness/mode;
resident firmware arbitration, thermal/power limits, request completion and
recovery; physical clock change; Windows performance-provider integration.
The next bounded UEFI design uses the stock domain0 enable1 handshake,
holds1300000kHz, briefly requests the immediate next stock OPP1400000kHz,
restores1300000kHz, then returns to the same menu for BootAndroid. Its first
integration is a separate **read-only PMU observer**, with zero PMU/controller
writes. Counter-disabled/trapped/unqualified results must not lead to clock
writes. A reset watchdog can recover a trap but does not promise an ordinary
return to the menu. The offline model, measurement/thermal gates, restoration
contract and concrete integration gaps are in
`diagnostic/degas-dvfs-audit/clock-step/README.md` and `model.py`.
Five host model tests pass. Keep PLLs, voltages, GZ and early firmware unchanged.

Validation: existing decoded captures were read and their request-index LUT
matches recomputed on the host; the module SHA/build-ID checks in the existing
audit loader passed while inspecting original helper instructions. No new
hardware test was run.
