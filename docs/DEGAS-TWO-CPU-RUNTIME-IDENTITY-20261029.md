# Two-CPU runtime identity experiment, 2026-10-29

Goal: validate that the AP pre-entry addresses are interpreted against the
actual running kernel build before changing any Windows startup prerequisites.
This is diagnostic firmware, not an SMP fix. The working CPU7 fullscreen
image remains `artifacts/degas-prime-fullscreen-v1/boot.img`, SHA256
`d4297ff9f41b894087ec259ecfa5bf141404bb0ee43fcc950bbb85213291485e`.

## Audit results

- Fresh Android read-only capture in
  `evidence/raw/2026-10-29-degas-active-boot-identity` confirms the active
  ESP BCD default is installed Windows 11 Pro, with no one-time bootsequence,
  no kernel/HAL override on that loader, and the expected winload path.
- Installed ntoskrnl still matches the audited file SHA256
  `5bb49f8383e38b26ef0c18f3087db3be541e99143b80b92d2f83d824b7e5301c`.
  This does not yet prove the prior crash's runtime build.
- The second HalStartNextProcessor caller is a power-resume path, not an
  earlier normal cold-start path; see DEGAS-AP-RELEASE-AUDIT-20261029.md.
- PRCB+1908 can legitimately be zero before KiSystemStartup. The unexplained
  field is +8300, conditional on matching runtime build and address view.
- The audited kernel already cleans and publishes its copied startup stub;
  no generic additional cache flush is justified by current evidence.

## Isolated image

Build configuration:

```
DEGAS_BOOT_CORE=0
DEGAS_CPU_TEST_LIMIT=2
DEGAS_CPU_PARKING_EXPERIMENT=1
DEGAS_WINDOWS_IDENTITY_OBSERVER=1
DEGAS_DIAGNOSTIC_EBS_WATCHDOG=1
```

Only CPU0 and CPU1 are enabled in MADT. CPU1 is started only when the Windows
menu action prepares Parking. CPU2..7 remain off. Eight private pages are
still allocated to preserve the qualified mailbox/journal layout; only
enabled CPUs receive a parking version/address. The observer does six bounded
64-bit reads with the qualified private coherent mapping and restores the
same architectural state before branching to Windows. It does not modify
Windows instructions, LPB or PRCB. Its targeted CIVAC has the same diagnostic
visibility side effect as the prior observer; it is not purely passive.

Snapshot layout for this mode (do not interpret it as the older PRCB mode):

| Offset | Value |
| --- | --- |
| E00 | error in low32 / failed read number in high32; zero on success |
| E08 | Windows-saved entry PC |
| E10 | first instruction qword at that PC |
| E18 | DOS-header qword at candidate image base PC-A91BA0 |
| E20 | PE-header qword at candidate base+110 |
| E28/E30/E38 | complete 24-byte RSDS signature/GUID/age at base+4DC78 |

These fixed offsets test one specific saved build. Any failed read or mismatch
invalidates that interpretation; the decoder must not label a differing build
as the audited kernel. Successful identification is not a full runtime image
hash and does not by itself explain HAL. This mode intentionally does not
resample the queue fields in the same snapshot.

Host validation: actual PrepareCpuMadt compiled with ASan/UBSan for core0/8,
core0/2, core7/1; existing observer integration scenarios; new identity reader
emulation covers valid capture, a changed GUID, bad DOS header, missing RSDS
mapping, unsupported TCR and wrong EL, architectural-state restoration and
no writes to sampled RAM. Hardware behavior remains to be tested.

After testing: allow the configured 30-second EBS watchdog to reset, select
BOOT ANDROID in the next menu, and retain the cable. Do not select CPU/RAM
tests. Collect the journal, validate its source manifest/attempt/CRC with
ap_journal_v1.py, then run decode_runtime_identity.py on that decoded JSON.
User need only report the stop code, not copy hexadecimal addresses.

## Built and flashed

`artifacts/degas-two-cpu-runtime-id-v1/boot.img`, SHA256
`bf2c5efad768bf759048d1bd294d15e479cf976c7b260645713a76b180c3e70d`.
Final firmware contains the exact emulated 1048-byte identity observer once.
Identity switch is supplied to both CC_FLAGS and PP_FLAGS: the latter is
necessary for EDK2 assembler preprocessing. The first intermediate build
omitted that switch for assembly and was rejected by the linked-byte check;
it was not flashed. Final build completed successfully, preserved stock AVB
tail, product degas / active slot b checked; flash boot_b and reboot succeeded.
Awaiting the user's Windows menu selection and subsequent Android capture.
Source manifest SHA256
`91244693f031242443f80ed34a8fdb09189817e49aa57a824995e9f3c2cb923e`.

## Hardware result

User reports HAL_INITIALIZATION_FAILED (5C) with two enabled CPUs. BOOT ANDROID
then returned to UEFI instead of starting Android. Product degas / slot b
verified in fastboot; exact stock boot_b restored and rebooted successfully.
Android ADB is available. No partitions or Windows files were modified.

Fresh capture `evidence/raw/2026-10-29-degas-two-cpu-runtime-id-v1-result/`:
all five collector commands completed. Slot30 was exported as 2021-byte
`dmesg-ramoops-30`. Source-manifest/attempt/CRC/metadata validated;
AP1 committed successfully, other slots uncommitted as expected for this test.
Attempt `3112643c-7dfa-4ce1-8000-000000000001`.

Runtime-identity reader status zero; ALL comparisons matched the saved build:
entry instruction, DOS header, PE header and RSDS GUID/age.
Saved PC `FFFFF8037AC91BA0`, verified candidate NT base `FFFFF8037A200000`,
GUID `00D46CC0-E2A1-3E48-6AC6-C224799CFC23`, age1.
This establishes runtime build identity for this attempt; it does not prove
the older queue snapshot has identical values in this new attempt.

The captured console independently shows enabled2, parking aps1,
identity_observer1, 30-second EBS reset armed and EBS success. It contains
only one P0 wrapper entry and NO Android request marker or later UEFI entry.
Its only complete raw console frame is the pre-initialization invalid frame;
the independent AP slot is the valid source of this result. Therefore do not
claim the Android flag was lost, the Android kernel crashed, or that a later
UEFI invocation was captured. The return-to-UEFI failure remains unexplained.
