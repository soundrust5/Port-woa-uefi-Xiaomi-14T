# Native Windows context reconstruction — bounded cold-start experiment

This carries the Linux shared-memory principle into the pinned Windows ARM64
bootstrap. It does not replace GZ, ATF, TEE, LK or preloader. It does not patch
Windows files on disk. It is disabled by default (`DEGAS_NATIVE_CONTEXT_BRIDGE=0`).

## Hardware result and expansion

V2 now also succeeded on hardware: the user confirmed **8 logical processors**
in Windows Task Manager and substantially faster Windows. This is a human
hardware observation, recorded in `artifacts/degas-native-context-v2/user-observations.json`;
the v2 retained installation record awaits the next return to Android.
Working v2 image/source ZIP/linked EFI modules are preserved separately from
the two-core control. No further reboot is needed merely to answer the user's
cache/frequency question.

The v1 experiment is successful on the phone: the user observed two logical
processors in Task Manager, desktop operation and quicker OOBE completion.
The matching retained native record reports stage3/status0/patched1, one exact
kernel match and no access qualification failure. Captured data is in
`evidence/raw/2026-10-29-degas-native-context-v1-result`.

The proven v1 boot image, original sources ZIP and linked EFI modules remain
in `artifacts/degas-native-context-v1`. The eight-CPU v2 uses the same 56-byte
prefix, same fixed eight-page allocation, and same native PSCI method. Only
the supported configuration count expands to8; Windows now reports all8.

## Implemented integration

The v1 configuration enables CPU0/CPU1; v2 enables CPU0..CPU7. Both leave native PSCI
advertised and all APs OFF, and reserves eight low ACPI NVS pages. Full GICC
UID/interface/affinity/enable bits and all eight firmware affinity states are
qualified before publication. GICC mailbox addresses follow physical Aff1.
The exact inspected Windows HAL consumes these addresses even with PSCI=1.
This is build-specific compatibility behavior, not a general ACPI promise.

Before Windows starts, firmware allocates two map buffers. BeforeExit captures
the memory map and sorts its private copy. The EBS event uses no Boot Service,
allocation, disk access or permission change. The existing 30-second hardware
watchdog is armed before any experiment access. A failed first EBS attempt
invalidates the snapshot; because this Mu emits BeforeExit only once, any later
successful retry intentionally aborts under the watchdog. Retry support is not
claimed by this experiment.

The locator examines only loader-code/data pages in that immutable map, with a
1 GiB page budget, and requires one uniquely matching mapped kernel. Header,
section, linked CodeView record, normalized full 384-byte native AP template,
and related native-path instructions must match the captured active build.
The complete SizeOfImage must have contiguous loader ownership. The high NT
relocation VA is distinguished from its physical backing and never dereferenced.
Mismatches, ambiguity, inaccessible reads and protection failures stop the boot.

GetMemoryMap in this Mu strips actual RP/RO bits, so descriptor attributes alone
are insufficient. Every actual reader/writer additionally uses AT S1E1R/W to
qualify the current EL1 identity mapping. The probe requires EL1 and PAN=0,
preserves PAR_EL1 and changes no page permissions. It qualifies stage1 access;
GZ stage2 backing/permissions and EL2 trapping remain hardware observations.

Mode2 replaces only 56 bytes in the loaded native template, restoring mailbox
X0 from MPIDR before continuing the original Windows body. It verifies the
replacement and publishes data/instruction caches. No AP is prestarted or
parked. The real Windows CPU_ON call and its return status remain unchanged.
A failed write/readback may leave a partial replacement and therefore always
stops under the watchdog; it is never treated as an untouched fallback.

## Evidence and remaining limits

Agent prototypes separately execute original GZ context loss and shared-memory
reconstruction. The exact Windows fixture tests its original generated startup
page and its adapted prefix through stack/PCR setup before enabling the MMU.
Firmware core tests compile the actual production header under UBSan and cover
identity, domain ownership, uniqueness, faults, protection and retained records.
The actual ACPI preparation function is separately tested with firmware stubs.

EBS image visibility and Windows acceptance of all8 CPUs now have hardware evidence.
Long-term Windows integrity checks remain unproven. The
source template resides in persistent kernel .text; late integrity/HVCI/
PatchGuard acceptance remains unresolved. The first experiment is cold boot
on two CPUs; the next cold boot successfully expanded to8. This does not yet
establish a generally stable eight-core implementation across power transitions.
Sleep, hibernation, hotplug and indirect restart consumers are excluded.

The existing Windows UFS driver intentionally disables the watchdog in
FindAdapter (degasufs.c307) before writing to disk. Thus the30second EBS timer
only protects early failures before that driver takes over; a successful
Windows boot stays running. Return from that state using normal shutdown/
restart and Boot Android, rather than waiting for an automatic reset.

## Retained installation record

`NativeContextRecord.h` writes only bytes0..511 of the existing slot30 at
480AE000. The Android one-shot flag at +F00 is untouched. Native preparation
at menu entry does not overwrite a previous result. Stages1/2/3 share a single
boot-attempt sequence, source hash and fresh build UUID. Stage3 records locator
and patch status, not Windows processor acceptance.

Decode raw-slot bytes with `decode_record.py --raw-slot`; the DBGC outer commit
and inner CRC must both pass. A pstore payload may omit the outer header and
is explicitly labeled payload-only by the decoder. Supply the corresponding
build manifest so a stale capture cannot be accepted as the current attempt.

The paired build-config/source manifests and verified stock boot_b backup are
kept in `artifacts/degas-native-context-v1`. Phone flashing, if performed, must
be restricted to the ordinary recoverable boot_b image and recorded separately.
