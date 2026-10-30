# Optional topology integration review, 2026-10-29

The reviewed `DegasAcpiDxe.c`, `CpuPpttGenerated.h` and `DegasSmbios.c` layout passed source checks. Compilation and publication to the OS require separate validation.

- Parsed the actual generated initializer: signature PPTT, revision3, declared
  and actual length216, checksum0. It contains one20-byte root at offset36 and
  eight20-byte core nodes at56..196, with no trailing or missing bytes.
- Root flags1 declare one physical package, not a leaf, with no valid UID.
  Its IdenticalImplementation flag is clear, which is appropriate for the
  mixed A510/A715 package. Core flags0A declare valid UID plus leaf, without
  SMT/thread or physical-package flags. All parents point to offset36; UID0..7
  are unique and match the existing MADT UID scheme. Private-resource counts
  are zero. No cache geometry or homogeneous package implementation is claimed
  by this PPTT.
- The opt-in adds the fifth ACPI source and uses `ARRAY_SIZE(Tables)` for keys,
  validation, installation and reverse rollback. Published-table verification
  requires exactly one PPTT of the expected size, checksum and complete node
  payload; the required mask becomes15. A table-install or verification failure
  includes PPTT in the existing reverse uninstall loop. The option-off set
  remains the original four tables.
- SMBIOS has eight records with the option off and nine with it on, so
  `Added[9]` is sufficient. Handle2007 is distinct from every other handle in
  this producer and Type4 references it only under the same option. It is
  added before Type4 and removed after Type4 during reverse rollback. A handle
  collision causes Add failure and removes only this attempt's prior records.
- Type7's legacy and extended size fields both contain4096 with their64KiB
  granularity bits clear: each describes4096KiB, or4MiB. The packed local
  Type7 declaration is27bytes, within the configured SMBIOS3.6 format and
  helper's length/buffer limits. Configuration0382 selects enabled internal
  level3 with unknown operating mode; unified type and unknown associativity,
  SRAM and error-correction fields are consistent. Type4 remains one SoC
  processor record with eight physical cores/threads; clock and enabled-count
  unknown fields remain unchanged. The Type7 change supplies no frequency.

The reviewed topology option does not alter native PSCI, AP entry/context
recovery, CPU-start calls or the working eight-CPU bridge. Its PPTT represents
physical topology even when existing diagnostic MADT gating disables CPUs;
the MADT still determines enabled CPUs.

One pre-existing error path is outside this option's table rollback: failure
to install the native-context protocol returns after ACPI publication rather
than entering `Rollback`. This is unchanged by the topology addition and is
not a request to modify the preserved native bridge in this task.

This is a bounded source/layout review. It does not predict whether Windows
will display the SMBIOS L3 value or establish additional cache measurements.
Cache hardware probing remains frozen. No phone operation was performed.
