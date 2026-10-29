# Diagnostic ACPI mailbox startup candidate

Motivation: actual AP probe v2 executed on all seven secondary affinities,
but every incoming x0 was zero despite nonzero CPU_ON context_id. Context
loss is observed; its role in Windows's failure is still a hypothesis.
The installed kernel's CPU_ON wrapper forwards the context argument from
its caller. Disassembly callers14047f900 and14048211c load that argument
from per-processor data; this is not a trace of a particular live boot.
Latest user's slow-UI report did not independently measure current CPU count.

This opt-in firmware experiment exposes ACPI ARM MP-startup mailboxes instead
of advertising a compliant PSCI CPU_ON interface to Windows. Microsoft
requires supported PSCI to be used; this experiment investigates a platform
where the observed context-transfer contract is not compliant. It is not a
claim that parking should replace working PSCI on other systems.

References:
- https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/uefi-requirements-that-apply-to-all-windows-platforms
- https://raw.githubusercontent.com/u-boot/u-boot/master/arch/arm/include/asm/acpi_table.h
- https://raw.githubusercontent.com/u-boot/u-boot/master/arch/arm/cpu/armv8/acpi_park_v8.S

## Implementation and boundaries

Default build remains experimentOFF. Buildscript accepts environment
DEGAS_CPU_PARKING_EXPERIMENT=1 explicitly. BootServices-only CPU protocol
version2 publishes a callback only in that build. Only explicit Windows
boot selection invokes it, before loading Windows boot manager. No APs
start in SEC or during normal menu display. CPU probe remains separate.

Callback requires eight enabled MADT entries and all seven targetsOFF.
Allocates eight contiguous4KiB NVS pages below4GiB, mapsUC, places private
80-byte position-independent code at each page+0x800. Does not trust incoming
PSCI x0: ADR derives the physical mailbox from its own code location.
Firmware-owned progress/Mpidr at+0xc00/+0xc08 prove each AP entered.
Initial page fields are CPU ID=0xffffffff and entry=0. AP waits until CPU ID
changes and entry is nonzero, clears entry, then branches with x0=mailbox.
OS-owned bytes16..2047 are never changed by firmware. No stack or C runtime,
no clock/voltage/GIC writes. Polling/YIELD avoids depending on initial IPI
delivery; CPUs remain powered until OS releases them or system resets.

Seven CPU_ON calls use context0 intentionally (callback needs none), and
each AP's MPIDR/ready record must match within500ms. No OS boot proceeds on
partial readiness or ACPI installation failure. All allocated code/mailboxes
remain NVS; they are never freed/reused while APs might execute. Existing
watchdog reset path handles boot failure. After allAPs ready, reinstalls
FADT with ARM boot architecture flags0 and MADT GICC parking version1 with
the corresponding per-CPU NVS address. UID/GICR/MPIDR unchanged. Published
RSDP/XSDT/FADT->DSDT checks rerun. No mixed PSCI/parking advertisement.

This does not implement Windows CPU idle, resume, hotplug, thermal control,
frequency management, reset or power-off support. OS shutdown/restart may
need separate platform support once PSCI is no longer advertised. It is a
startup diagnostic candidate, not a complete power-management solution.

## Validation

Firmware build succeeded21:50:05 Moscow. Executed assembled AP code under
Unicorn in42 scenarios: seven affinities, addresses below/above4GiB, complete
handshake versus entry-only/ID-only. Tests check no premature branch, x0
equals mailbox, jump word clears, firmware progress/Mpidr correct, and all
OS-owned canary bytes unchanged. This emulates the AP code only, NOT Windows,
PSCI firmware, GIC, cache coherency, or the phone. Confirmed all80 tested
bytes exactly match linked DegasAcpiDxe.efi at RVA1010..1060.

Image artifacts/degas-cpu-parking-v1/boot.img SHA256
0d84322d75a60c74077ebd748bff41577ee1fbd265fdf5f3fa5e730cff711393.
Stock AVB tail retained. Not yet flashed at report creation: Windows is
running, awaiting user return to fastboot. No installed Windows file/driver
or CPU frequency setting changed by this preparation.

Hardware success requires seven READY records, then OS-reported active CPU
count8; UI speed alone is not the acceptance criterion. Failure must preserve
log and return to the known stock Android or previous UEFI image.
