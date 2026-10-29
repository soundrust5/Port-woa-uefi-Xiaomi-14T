# MT6897 Windows UFS first read-only miniport

Status: compiled, test signed, host tests passed; hardware result pending.
User requested working Windows UFS, then a controlled write test on the dedicated
Windows partition. Do not claim this version supports writing or native boot.

## Baseline

WinPE installer boot confirmed on `artifacts/degas-windows-no-wdt-v23/boot.img`.
Display is the working autonomous GCE loop; EBS watchdog remains disabled.
Phone is Xiaomi 14T / degas, serial XC7TUCXG95U8IVMN, boot slot b.
Stock recovery image: `evidence/raw/2026-10-24-degas-return/boot_b.img`, SHA256
`fa01819342dd24164dea568f88f9715bc3132f738a68c49453907982946c1113`.

## V24 design

* ACPI DGS0001, MMIO112b0000/3000, primary GICINTID200, CCA0, no genericCID.
* At EBS, stop both queues, disable interrupts, require no pending doorbells,
  clear firmware DMA bases and retain initialized HCE/link. Failure stops HCE.
* Own ARM64 Storport miniport `diagnostic/degas/ufs/windows/degasufs.c`.
  No patched Qualcomm binary, no vendor SMC or PHY writes in Windows.
* StorPortGetUncachedExtension with verified low32-bit contiguous address,
  1KiB UTRL alignment, own UCD/response/PRDT/data buffer. Does not reuse44470000.
* One slot, half-duplex callbacks, timer polling, elapsed one-second command
  deadline; timeout quarantines controller and never reuses in-flight DMA.
* Read-only CDB whitelist; WRITE/UNMAP/FORMAT/security/vendor and DATA_OUT denied.
  MODE SENSE forces write protection. Physical LUN0/1/2 exposed as LUN0/1/2.
* Init requires live firmware handoff and verifies NOP; no cold init, resume,
  crashdump or native Windows boot qualification. Reset quarantines, returns
  failure rather than claiming recovery. Restart deliberately rejected.

## Tests and deployment

`tools/test_degas_windows_ufs.py --mutations`: actual extracted C functions run
under GCC/Clang ASan/UBSan. Tests reject out-of-range/overflow reads, writes,
malformed responses, old unpadded PRDT and write-enabled mutations. These are
host mocks, not a proof of physical DMA or Windows ABI operation.

Build: `tools/build_degas_ufs.sh`; package: `tools/package_degas_ufs.py`.
INF passes InfVerif /w, binary/catalog signed with existing project development
certificate. Not Microsoft signed. Host trust stores were not modified.

`tools/package_degas_winpe_log.py --driver-package artifacts/degas/ufs-windows/test-package`
packages a copy of pristine WIM index2. Offline SYSTEM policy SAN3/NoAutoMount1.
RAM-only logger checks MiniNT/ARM64/exactDGS0001/policies and pinned certificate,
then drvload. It queries disks and reads first64KiB/last32KiB per known LUN to
compare CRCs against the fresh Android snapshot; no disk online or Setup.
Logs: X:\DegasLogs. They do not survive hard reset.

Reference: `evidence/raw/2026-10-27-degas-windows-ufs-baseline/reference.json`.
LUN0/1 4MiB, LUN2 264509587456 bytes; sector4096. Main GPT disk GUID is zero;
that is not grounds for rejecting it. Both GPT copies validated in Android.

Phone ESP staging verified: `artifacts/degas/ufs-windows/staged.json`.
Only sources/boot.wim and EFI/Microsoft/Boot/BCD replaced. Old files retained as
`boot.wim.before-degasufs` and `BCD.before-degasufs`. Original BCD/WIM also on PC.
BCD change only test signing on default WinPE loader; host BCD untouched.
All four GPT header/entry-array comparisons passed unchanged after staging.

Firmware: `artifacts/degas-ufs-windows-v24/boot.img`, SHA256
`4232b738d4717ebb9b09ed1e2f11f8252fc9720c2598e0e9a48974d41c859df1`.
WIM SHA256 `379f09238deaf0bbaa1e46add72870bc59056976df1a1086218652b8dbcd4a33`.
Signed driver SHA256 `f8dc3209b5074e3a4bf70377e288532fcf82733518fd18fb4aa8c45151387da0`.

Next: user launches Windows Boot Manager, waits90s, records driver install code,
Started/problem, disks, readmask and UFS READ. Only after verified reads, develop
controlled writes constrained to the dedicated Windows partition and validate
persistence independently from Android. No write experiment on Android data.

## V24 hardware result and V25 diagnostic follow-up

User observed DGS0001 Started=0, problem31, ProblemStatus=C000000E;
Microsoft controller 2/2 is a different device and not evidence of UFS success.
DriverEntry incorrectly selected Internal; changed to ACPIBus, matching the
actual ACPI-enumerated device. Hardware confirmation is still pending.

V25 adds driver-only records to reserved ramoops console 0x480af000/0x40000.
It records DriverEntry, Storport registration, resources, DMA allocation,
initialization, NOP and transfer results. This is not a complete Windows log.
A diagnostic WDT resource at 0x1c00a000 is assigned to DGS0001. FindAdapter
arms 30 seconds; timer quiesces read-only UFS at 27 seconds. Stop cancels the
idle timer and late callbacks exit before touching MMIO. This timed build must
not be reused for write tests. WDT is not armed if FindAdapter is never called.

V25 firmware boot.img SHA256:
f4f7dc65234fccfd97184a710513e0db7515efb57d717654c5651a0659bf5fd3
Signed driver SHA256:
ba79db8484afe0e3e545af562e40e7559dbae57fc679649257856c0edd1f96f6
Manifests: artifacts/degas/ufs-windows/package.json and
artifacts/degas/winpe-log/package.json. All writes to physical UFS through
this Windows miniport remain blocked; only ESP deployment by Android is used.

## V25 hardware capture and V26 request tracing

Capture: evidence/raw/2026-10-27-degas-ufs-v25-result/console-ramoops-0.
DriverEntry and StorPortInitialize returned success; FindAdapter received ACPIBus
and both assigned ranges. WDT armed. HCE=1/HCS=0x10F/IE=0.
NOP PASS with private DMA PA=0x4442E000. At 27s quiesce: Completed=0,
Timeouts=0, doorbell=0. User reports Started1/problem0/disks0.
This confirms live-link NOP/DMA operation, not SCSI data reads or disk discovery.
The old log did not record early SRB rejection, so it cannot distinguish absent
requests from mapping/validation failure. V26 only adds BuildIo/StartIo and
completion traces plus request/submitted/blocked counters; firmware remains
V25. No unproven storage fix or writes enabled. Host sanitizer tests pass.

## V26 result / V27 short REPORT LUNS completion

Capture: evidence/raw/2026-10-27-degas-ufs-v26-result-retry.
Only SCSI request observed: REPORT LUNS (A0), BTL0, 64 bytes.
BuildIo mapping succeeded. Synthetic reply returned 32 bytes with SUCCESS.
Next request Function0x17 is WMI, rejected unsupported; it is NOT PnP.
No INQUIRY followed. NOP remained the only hardware submission.

V27 corrects short REPORT LUNS status to SRB_STATUS_DATA_OVERRUN while keeping
actual DataTransferLength=32 and SCSI GOOD. This is a verified contract defect,
not yet a proven cause of missing disks. Microsoft documentation:
https://learn.microsoft.com/en-us/windows-hardware/drivers/storage/error-handling-in-storport-miniport-drivers
Host test covers captured 64-byte request plus exact32 and tiny allocations.
Same V25 UEFI/WDT; read-only restriction unchanged. Hardware test pending.

## V27 hardware result

Capture: evidence/raw/2026-10-27-degas-ufs-v27-result.
REPORT LUNS requested64/actual32/header24 now completes status0x12 (underrun),
but still no INQUIRY. Only later request is unsupported WMI0x17. Pre-reset:
Requests2, Submitted1 (NOP), Completed0, Blocked0. Thus short-status correction
alone does not resolve enumeration. No SCSI command has reached UFS yet.
Do not label this a UFS link timeout or proof of hardware failure.

Next investigation: returned REPORT LUNS contents as observed by Storport,
including mapping/bounce-buffer effects for CPU-filled replies with
Dma64BitAddresses=0, plus enumeration configuration. Cache/bounce theory is
unproven. WDK StorPortFlushDataBufferMdl is a no-op on ARM64; do not add it as
an assumed fix. Phone restored to stock boot_b/Android, no further flash.

## V28 contract audit and mapping diagnostic

V27 still stops after REPORT LUNS; its corrected underrun status did not
resolve disk discovery. No INQUIRY or media read was submitted.

V28 aligns MaxNumberOfIO, MaxIOsPerLun and InitialLunQueueDepth to one.
Non-SCSI requests no longer read CDB/ScsiStatus or overwrite ScsiStatus before
rejecting unsupported request functions. Unsupported WMI remains unsupported.

For REPORT LUNS only, BuildIo records the mapped CPU buffer physical address
and first Storport scatter/gather address/length. This tests whether mapping
or DMA bounce behavior could affect a CPU-generated response. Different
addresses alone are not proof of a bounce overwrite (translation is possible).
No cache fix or DMA-address change is presumed correct without device evidence.

Matching Microsoft storport.pdb was obtained from the Microsoft symbol server
for the PE storport binary (D10E2677AD9EB08EB040A951AF36A0D81). Inspection of
its enumeration parser is consistent with the current three-LUN response
format. This does not establish what bytes it actually observes at runtime.
Artifacts: artifacts/degas/ufs-windows/storport-audit/.

GCC and Clang host sanitizer tests passed (1,769,688 driver checks and 271,144
retained-log checks each). These mocks cannot validate Windows DMA semantics.
ARM64 build, INF validation and test signing completed. Signed SYS SHA256:
ddc92135cd22ddb43a27ae11f4163577633be5081b51ed6beafd2c1f5b399e25.
UEFI remains the verified V25 image. All Windows media writes stay blocked;
the diagnostic driver still quiesces at 27 seconds and resets at 30 seconds.
Hardware result pending.

## V28 hardware evidence / V29 isolated DMA mapping change

Valid capture: evidence/raw/2026-10-27-degas-ufs-v28-result-retry.
Initial capture was interrupted by Android ADB restart; preserve but do not
use its truncated output. Retry pstore and debuglogger copies have matching
SHA256 45e52f3ca53dba7e12bd146a6a2e5127dc043bc1db1dc8c88f9aee34b412867c.

Queue limits are all one. NOP passes. REPORT LUNS CPU PA=0x169d3f280,
Storport SG DMA address=0x4443f280, length64. CPU writes header24/actual32;
completion status0x12; still no INQUIRY. This establishes distinct CPU/DMA
addresses, not by itself the exact completion overwrite mechanism.

V29 changes only Dma64BitAddresses to SCSI_DMA64_MINIPORT_SUPPORTED and adds
one configuration log. Installed WDK storport.h explicitly documents that
this flag keeps Uncached Extension/SenseInfo/SrbExtension below4GB while
I/O transfers are not remapped. Our SRB data path handles high addresses via
CPU mapping/copy and never supplies SRB addresses to UFS; all controller DMA
continues through the existing checked low uncached arena. No FULL64 flag,
new hardware DMA range, raw mapping of an SG address or cache workaround.
This tests whether 32-bit bounce copy was replacing CPU-filled replies.
Hardware confirmation is still pending. Writes remain blocked.

ARM64 build, INF validation and GCC/Clang sanitizer suites passed.
Signed V29 SYS SHA256:
e03e4b39ba48e014ac1a6cecfac6821cf70b47f5966cf0bc6afba273b7204d53.

## V29 result: payload64 did not resolve discovery

Capture evidence/raw/2026-10-27-degas-ufs-v29-result completed successfully.
Pstore/debuglogger SHA256 agree:
147c5cdcdfad34fa2ad0e328c21f2b70df36d02e62cfa44a959d7886c91332e7.
Config flag1 applied; low private arena0x4442f000; NOP passes.
REPORT CPU PA0x168d09890, SG address0x17ffce890, both above4GB and distinct.
Response32/header24/status0x12. No INQUIRY; only subsequent WMI0x17.
Thus removing the 32-bit restriction alone is insufficient. Do not claim
confirmed bounce overwrite or fixed enumeration. Mapping/cache behavior and
software reply completion path require further investigation. Stock boot_b
restored and Android running. No further flash at this checkpoint.

## V30: two-agent binary audit identifies incorrect CPU buffer selection

Verified the audited storport.sys is identical to source boot.wim index2:
SHA256 7566a515bd71228bd1895b2afbd4a70fe5918e4444d8f5501f863bbe49415788.
Agent lun_audit confirmed REPORT LUNS header/entries and DATA_OVERRUN are
accepted by matching enumeration code; no padding/status workaround needed.

Agent dma_audit traced RaidXrbSetDataBufferAddress (140005438) with MapBuffers3
through DmaGetVirtualAddressForSgl (14002bf28), which calls DMA operations
BuildMdlFromScatterGatherList and maps the actual SG MDL if different.
Legacy Srb->DataBuffer is replaced at140143378. StorExtGetSystemAddress instead
uses original MDL at internal+68. Agent lun_audit independently confirmed
this mapping happens at1400055f8 BEFORE PostScatterGatherExecute140005608
and miniport BuildIo callback140002388..23ac.

V30 selects STOR_MAP_ALL_BUFFERS_INCLUDING_READ_WRITE in both config and init,
then uses Srb->DataBuffer in BuildIo. Nonzero data with NULL mapping fails
closed; no fallback to original MDL. The controller still only uses private
low DMA arena. No MMIO/queue/reply-format/status/power/UEFI changes.
The source mapping mismatch is established; resolving disks still requires
hardware confirmation. No claim of successful Windows media read yet.

New host test models separate original/mapped response buffers and copyback;
NULL mapping test ensures no hardware access. GCC/Clang sanitizer suites pass:
1,769,698 driver checks and271,144 log checks each. ARM64 build/INF/sign pass.
Signed SYS0612d2b97015514a9549485242ce5d877be03f6f0f71870018217b17681dc5b2.
References:
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nc-wdm-pbuild_mdl_from_scatter_gather_list
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/ns-storport-_port_configuration_information
Do not call the reserved DMA API from miniport; Storport owns that operation.

## V30 hardware READ PASS — confirmed mapping fix

User reports all disks visible and UFS READ PASS. Full capture:
evidence/raw/2026-10-27-degas-ufs-v30-read-pass.
Pstore and debuglogger copies both SHA256:
9fef5665034ebcb1a578be25df68ee1d89f36c1bfe3fec5b840ff1564195ab6a.
REPORT CPU PA and DMA address now equal0x169ded4c0. REPORT is followed by
INQUIRY on all3 LUNs, capacity and READ10 requests. At quiesce Completed=0x45
(69 successful data commands, including inquiry/capacity), Timeouts=0,
doorbell=0, Requests=0x64, Submitted=0x4b includingNOP, Blocked=3.
These blocked commands include SYNCHRONIZE CACHE0x35, deliberately not
supported in this read-only diagnostic. Some PnP/IOCTL remain unsupported;
READ PASS does not qualify power management, boot, or writes.

This hardware A/B result confirms the original-MDL mapping bypass prevented
enumeration in V29; actual mapped SrbDataBuffer resolves it in V30. Width-only
change did not solve it. No further firmware/MMIO/timing changes were needed.

Working source/driver/signing/deployment manifests frozen in:
artifacts/degas/ufs-windows/read-pass-v30/.
UEFI remains artifacts/degas-ufs-windows-v25/boot.img.
Stock Android boot_b restored after watchdog. No further flash this turn.
Next write validation must use only dedicated Windows partition and first
remove the unconditional30s watchdog, implement bounded WRITE+real flush,
retain fail-closed range checks and independent readback after reboot.

## V31 first bounded write qualification (pending device test)

Android verified the prior GPT plan unchanged; sdc96 starts at29796352*4096
and is142446919680bytes, unmounted, no blkid signature. Baseline captures
LBA29796607..29796609 in evidence/raw/2026-10-27-degas-ufs-write-baseline.
The middle block is zero; original three blocks and expected pattern retained.

Driver only permits WRITE10, LUN2, LBA29796608, count1,4096bytes, DATA_OUT,
zero reserved/control fields. All other writes and destructive commands remain
rejected. UPIU write direction0x20, UTRD direction1, copy mapped SRB data to
private DMA arena BEFORE submission, no copy-back for WRITE completion.
SYNCHRONIZE CACHE10 with all-zero CDB parameters is allowed only onLUN2.
Mode sense still reports write protection; generic filesystem writes not enabled.
The diagnostic30s watchdog is disabled and read back; no timed reset duringwrite.
This is a bounded raw block test, NOT filesystem or general-write qualification.

ARM64-only PE probe first requires READ mask7, successful exact-driver bind,
matching known disk size/blocksize, SCSI address0:0:2 and pre-write CRC.
It attempts one write only, then real synchronize-cache command, then READ10
and bytewise comparison. No automatic write retry. Stage6/error0 means
write/flush/readback passed in PE, durability still needs Android verification.
The x64/selftest path never runs this test. Disk stays offline, no format/Setup.

Host GCC/Clang sanitizer checks1,769,817 plus retained271,144 each passed.
Windows driver and ARM64/x64 logger compiled, INF/test signing passed.
V31 signed SYS4b9add0117e751bc6f02b2b6d1b22fbe06c4d3215733391b4945797e7afda361.
Post-reboot check: tools/verify_degas_write_probe.py reads expectedblock and
both neighbors plus primary/backupGPT; writes nothing. It preserves readback.
Need user screen result before requesting manual reboot (no automatic reset).

## V31 WRITE PASS confirmed independently after reboot

User reported WRITE PASS in WinPE (write, synchronize-cache, readback).
Restored verified stock boot_b, booted Android, then ran read-only verifier
 tools/verify_degas_write_probe.py. All assertions passed:
expected_match=true, neighbors_unchanged=true,
primary_backup_gpt_unchanged=true.
Persisted 4096-byte block SHA256:
379102ecf3f526fe927bec4407fe318e7365dbf600ec61483e5507ec63c58309.

Frozen working source/driver/manifests in artifacts/degas/ufs-windows/write-pass-v31.
Android is running. Test marker deliberately remains in the authorized Windows
partition at LBA29796608; no restore/write was performed by Android verifier.
This confirms durability of the bounded 4KiB test, not general filesystem,
large/random I/O, restart/resume, or Windows boot-volume qualification.

## V32 bounded large / duration stress (hardware pending)

User authorized large, prolonged UFS tests before touch work. Scratch extent:
LUN2 LBA29800448..29865983 inclusive (256MiB), entirely inside sdc96/Windows.
Android verified GPT and partition start/size, unmounted target. Backup plus
one guard block each side saved in evidence/raw/2026-10-27-degas-ufs-stress-baseline.
Region preimage SHA c42855061765c085786b0b4c4699a90bfc18c7d202829521ad11bcc6cb68912a.
Stage tool verifies full region preimage again before deploying test WIM.

Driver bounds WRITE10 to that extent, LUN2,1..14blocks/request, exact length,
DATA_OUT only, zero reserved/control. No WRITE16/UNMAP/FORMAT/vendor commands.
Boundary sanitizer tests cover both ends, crossing end, otherLUNs, oversized
requests. Existing 4KiB proof frozen in write-pass-v31. No new MMIO logic.

Probe performs sequential fill of256MiB, real flush, full compare; randomized
locations and sizes1/2/4/8/14blocks with write/read comparison until600s elapsed
from start, then flush and another complete256MiB compare. Mixed writes capped
at524288blocks (2GiB); after cap, random reads continue for remaining duration.
Total write cap256MiB+2GiB. Per-LBA header plus deterministic byte pattern permits
full independent Android comparison. Mixed overwrites reuse the same pattern;
this is not an alternating-pattern/stale-overwrite test or queue-depth benchmark.
No automatic retry after error and no watchdog. UI reports stage/time/MiB/error.
Stage6/error0 is PE success only; verify_degas_ufs_stress.py must still verify all
65536blocks, both guards, and primary/backupGPT after reboot.

Host GCC/Clang sanitizers passed1,770,102 driver checks plus271,144 logchecks each.
ARM64 driver and ARM64/x64 logger build and signing passed.
Signed driver SHA2396b7cd143200419c229fd54e7ba9be6e8393bec33b141ece420cd6a2f9a48e.

## V32 result and V33 completion-latency candidate

User reports WRITE PASS, 675 seconds, zero errors. After restoring verified
stock boot_b and booting Android, verify_degas_ufs_stress.py verified all 65536
blocks: zero mismatches, both neighboring guard blocks unchanged, primary and
backup GPT unchanged. Expected/actual region SHA256:
a1054f8d5312a0852c315f7f90f64a5ad89dd4f2259fee88687b1b42594a32c2.
V32 sources and signed package frozen in artifacts/degas/ufs-windows/stress-pass-v32.

V33 candidate adds at most 50us immediate completion polling after submission,
then falls back to the existing timer. Response validation and buffer ownership
remain identical through Poll; scratch bounds and DMA arena unchanged. This
removes timer latency only for commands completing inside that short window.
Hardware throughput improvement is NOT yet measured; candidate not deployed.
## Full Windows installation prepared (2026-10-27 afternoon)

User explicitly authorized Win11 Pro ru-RU, local Xiaomi/no password and former
linux_root Windows partition. Two-finger Windows touch confirmed by user.
Final raw journal exported evidence/raw/degas-ufs-journal-20261027-163621.

IMPORTANT: Windows partition is NOW NTFS. NEVER deploy old raw-journal WinPE or
run scratch writes at29800448: those blocks now belong to NTFS. Original staging
tool now rejects NTFS. Dedicated installer PE has no raw journal code.

Installer WIM SHA25650c0fa460f0236c3a53e8a72a3aaa8f266c3c7a876708ba6e7db66aec6f2c30d
staged to ESP sources/boot.wim, BCD unchanged, GPT verified. Native bootstrap
validates exactLUN2/GUID/ranges, online/mountsW/S, checksNTFSfile64KiBwrite/flush/
unbufferedread, then invokes W:\DegasInstall\install.cmd. Noautoreboot.
Signed installation UFS candidate SHA256e765ab864ce2eeb115c084fbe4510a68460f33add00c5bb5429a52c4f882a82d
permits only Windows+ESP writes, realflush/shutdown/FUA; earlyfullboot unqualified.

Android formatted ONLY sdc96 withmkntfs quick sector4096/cluster4096/start29796352,
34777080sectors. GPT+neighboringguardsunchanged. BPB verified4096/1/29796352,
total34777079. Evidence/raw/degas-windows-format-20261027-164953.
Staticntfs-3g mountedrw,norecover; staged cleaninstall-pro.wim and16configuration/
driverfiles, verifiedeveryhash, unmountedcleanly. SourceWIM SHA256
4400c3d350b71fbbf09d9e700c8ff9b1264d92707400e8d8fbff7ce47e5d424e.
Proof artifacts/degas/full-windows/{installer-staged,formatted,ntfs-staged}.json.

SameverifiedUEFI artifacts/degas-touch-windows-v1/boot.img flashedboot_b and
rebooted. Awaiting userselectWINDOWSBOOTMANAGER; Windows image NOT yet applied.
Installer SUCCESS creates installed.ok and BCD.full but doesNOTpublishBCD.
AfterSUCCESS: collectlogs, verifyofflineUFSbootserviceanddriver, publishmatching
WindowsBootEFI files withbackups plusBCD.full preservingPEfallback. Currentold
bootmanager/newwinloadcompatibilitynotassumed. FullOSuserXiaomi computerDEGAS-PC,
sleep/hibernate/displaytimeoutdisabledbeforeOOBE, testcertoffline, testsigningBCD.

