# Historical Android GZVM RAM replacement

Source: Codex thread `019f6143-a149-7f03-8484-3e50284ec24f`, titled
`Привет. Я хочу попробовать чтобы ты попробовал написать uefi`.
Relevant transcript dates are July 2026. Selected original messages and the
actual replacement script are retained in
`evidence/raw/2026-10-29-degas-historical-gzvm-ram/transcript-extract.json`.
This is historical transcript evidence, not a new hardware experiment.

## What was changed

The old `tools/swap_gzvm_ram.sh` stopped Android Terminal, checked that crosvm
had exited, unloaded `gzvm` using `rmmod`, and loaded a modified Linux kernel
module using `insmod`. If module loading failed it attempted to reload the
stock module. A fresh phone boot restored the stock system_dlkm module.
The script itself was a file under /data/local/tmp; RAM-only refers to the
replacement of the running module, not to absence of all filesystem writes.

Recorded module fixes included incompatible reserved-field handling,
relative SPI interrupt conversion by adding 32, UNKNOWN-exit handling, and
later an experimental pin-before-map memory path. Separate changes to
virtmgr/crosvm process memory repaired guest PCI windows and exit handling.
Debian reached multi-user.target on one vCPU; later memory faults and
multi-vCPU failures were still unresolved in the retrieved turns.

The transcript also records an earlier vendor_boot DTB change enabling the
GenieZone device node. Therefore the complete historical enablement sequence
was not exclusively RAM-only, although subsequent module swaps were.

## Relevance to native Windows SMP

`gzvm.ko` is an Android EL1 frontend for creating and managing additional
guests through the resident GenieZone hypervisor. It is not the resident GZ
firmware loaded before LK. No resident GZ code replacement is demonstrated
by the retrieved script or matching transcript.

The current native CPU_ON context loss is in the resident GZ Linux-guest
PSCI handler, as recorded in DEGAS-GZ-HANDOFF-CONTEXT-AUDIT-20261029.md.
An Android module swap therefore does not directly replace that handler;
the driver is also absent when native Windows is running under UEFI.
A normal reboot to UEFI reloads the firmware and loses an Android-only
runtime modification. A different runtime route would require separate
evidence of access to the actual provider and of a suitable handoff.

The useful precedent is reversible testing with exact-byte verification.
It is not evidence that the current signed GZ firmware can be replaced by
insmod or that an existing EL2 patch mechanism has already been found.

No module was unloaded, no VM started, no firmware flashed, and no phone
reboot requested during this historical investigation. The old workspace
path C:/Users/gavso/Documents/EDK2_DEGAS is currently absent; the transcript
and script extract remain available even though that folder was not found.
