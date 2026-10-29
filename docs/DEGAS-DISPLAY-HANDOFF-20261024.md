# Degas display handoff, 2026-10-24

V3 restored stock boot_b; capture: evidence/raw/2026-10-24-display-snapshot-v3-result.
Initial ramoops CRC validated f65daf06; build 88756c9794bc4d33db5c4e7403959057a775d80be3a8bef374b6aca50ff224b9.
All 40 reads completed. Whole console SHA256 3e2ccd690c64d5db220c12b64ce3ce4a51acb55649744af82feaa1f280660913.

OVL0: 14402038=a98026d, pitch 1380, address fcc2f000.
OVL counterpart: 14602038=a98026d, pitch 1380, address fcc2f95c.
Offset 0x95c / 4 = 599 pixels, two 621-wide regions overlap by 22 pixels.
DSI0 retains command-mode state; all eight sampled DSI1 registers read zero.
All four mutex0 enable words are zero; module masks retained:
14001030=bc7423ff / +34=3; 14201030=9c6083ff / +34=3;
14401030=8007 / +34=0; 14601030=8007 / +34=0.
This establishes two configured image processing halves, NOT two active DSI links.
Android dmesg independently reports MTK_DRM_OPT_PRIM_DUAL_PIPE=1.

V4 adds seven read-only words: candidate retained LK code signature and mode
plus DSI0 busy/status and control. Candidate LK RAM addresses are NOT yet
validated as retained exact LK. Interpret mode only after signature match.
No MMIO/framebuffer writes. Host execution tests passed at two load bases.
Next: capture v4, then choose exact LK command-mode trigger branch; do not
mix dual-DSI setup with dual processing-pipe configuration.

V4 result: exact two-word LK code signature matches; retained mode 5082e570=1,
5082e548=0, 5082eaa8=0. DSI0 status +c=4 (busy bit clear), +190=0.
Capture evidence/raw/2026-10-24-display-snapshot-v4-result; CRC 9b41d46e valid.

Next display-frame test writes an orange MEDIATEK marker at x370..849,
y300..411, then exact single-DSI start/queue/trigger with scenario3 mutex0.
Only 14001000 and 14401000 mutex blocks are triggered, following LK scenario3;
second-pipe activation remains a possible follow-up if no visible frame.
Wrong mode/signature/OVL geometry or busy DSI blocks writes. Polling bounded.
Tests exercise two load bases, exact MMIO order, allowed framebuffer rectangle,
and no writes on mode mismatch or permanently busy DSI. This is not hardware
DSI emulation and does not prove visual output.

Frame v1 result: user saw unchanged MI. Valid CRC 2fb14aa0, all markers
complete. FRAME_TRIGGERED=80000004, IDLE_AFTER=0, STATUS_AFTER=80000004.
Command starts but does not finish within bounded polling; duration not measured.
Frame v2 isolated change: activate second processing pipe mutex0 as in LK
scenario18 helper (14201008/20,14601008/20), before primary pair. DSI1 remains
untouched. This is a hypothesis test, not proof scenario18 is current LK path.
Guards include second pipe module masks. Host success/rejection tests passed.

CONFIRMED visible frame, 2026-10-24: user reported orange MEDIAТEK appeared.
Frame v2 capture: evidence/raw/2026-10-24-display-frame-v2-result.
Build f73d4e9cbf69ef3abdc5fb9bc3ef46c621ee8c9c7c8c58a690aed22711d0516a;
boot SHA256 539f272ca06eb6bfa4d23fff1b62c9eac9afdade0457855e306364116a1483d1.
Ramoops initial CRC84929779 validated; whole console SHA256
0ae947dd228c2a70deb2123859b4741f7d908972f236ecf97e877610566e9750.
FRAME_TRIGGERED=80000004; FRAME_IDLE_AFTER=1; FRAME_STATUS_AFTER=16;
FRAME_DONE=1. Unlike v1, bounded polling observed completion.
Isolated change: second processing-half mutex activation. Single DSI retained.
Marker is only x370..849,y300..411; no full-screen repaint attempted.
This proves visible diagnostic output, not yet a booted Mu/GOP/menu.
Stock boot_b restored successfully and rooted Android capture completed.

Fullscreen diagnostic prepared after visible frame v2 success. Repaints only
1220 visible pixels per row, all2712 rows, pitch4992; padding unchanged.
Orange border and MEDIAТEK, labelled MENU PREVIEW ONLY / BUTTONS NOT ACTIVE YET.
No actual UEFI services, input or Android chainload claimed. Same proven
four-mutex single-DSI activation. Host bounds/order/guard tests passed at two
load bases. Watchdog retained; user must return fastboot for stock restore.

FULLSCREEN SUCCESS: user confirmed complete MEDIAТEK/menu preview visible.
Capture evidence/raw/2026-10-24-display-fullscreen-result, CRC314d1c09 valid.
Build1fc77b36b37811515175cdc0d16e1a7372f26e87044e83fa2639fcfe35b72286;
boot SHA2564f11ef804d7697256bfab76254ab122b28312e8b12049da46c224271828a45e8.
FULLSCREEN_WRITTEN=327c60 (1220*2712), IDLE_AFTER=1, STATUS_AFTER=16,
FRAME_DONE=1. Stock boot_b restored and Android/root capture successful.
Artwork is procedural C rectangles + 5x7 glyphs, no image asset or GPU.
Next engineering milestone: actual degas Mu bringup / GOP refresh integration,
then input and Android boot action. Current preview is not an interactive UEFI.

UEFI preflight prepared: same fullscreen renderer and proven display sequence,
followed by breadcrumbed read-only MRS of MPIDR/CNTFRQ/CNTPCT/CNTVCT/MMFR0/PFR0/
CPACR/CTR and entry X0-X3. No changes to timer controls/GIC/MMU. Purpose: verify
EL1 access before choosing Mu timer/platform configuration under existing EL2.
Legacy stone/MT6789 full image is still explicitly unsuitable for this phone.

UEFI preflight success: CRC66dfe421, PREFLIGHT_DONE=1. CNTFRQ=c65d40=13000000;
CNTPCT progressed 5579c07 -> 558870b; CNTVCT readable. Entry X0=47c80000;
MPIDR=81000000, MMFR0=22200101122, CPACR=330000. This tests reads, NOT timer
interrupt delivery/GIC configuration or enabling MMU. Android restored.

Started dedicated degasPkg/Drivers/DegasGopDxe: copied geometry-aware GOP and
added known single-DSI four-mutex refresh after writing GOP Blt operations.
Includes cache writeback (needed once UEFI enables caches), bounded100ms polling,
and hardware geometry/module-mask guards. Direct framebuffer writes bypassing
Blt are not yet refreshed automatically; driver not phone-tested.
DisplayCheck.dsc is explicitly HOST MODULE CHECK ONLY with donor dependencies;
it must never be packaged/flashed. Full degas SEC/memory-map platform pending.
GOP module compilation passed (DEBUG CLANGPDB AARCH64); saved artifacts/degas-gop-module-check/DegasGopDxe.efi. This is a DXE module, not a boot image. Build log evidence/build/mu-DegasGopDxe.log.

Real Mu first-DXE checkpoint platform created at mu/Platforms/Xiaomi/degasPkg.
FDF contains SEC + compressed FV with DxeCore, PcdDxe, DegasGopDxe and CheckpointDxe.
Common DSC compiles additional modules, but these are NOT included in this FD.
GIC PCD placeholders exist only to resolve unused common compile components;
GIC/timer interrupt drivers are NOT included or hardware-validated here.
PlatformSecLib is Null: no donor PMIC/watchdog/clock initialization. Watchdog
remains enabled. Dedicated SerialPortLib appends to initialized ramoops console.
Low image arena40000000..44480000 includes wrapper, FD40200000..40400000,
stack40400000..40440000 and heap40480000..44480000. Full expanded ARM64 Image
size reserves this arena with zero padding. DTB47c80000 and static reserves
>=48080000 excluded; framebuffer and console mapped separately. Framebuffer
mapping rounded up to4KiB (cea000) without touching beyond visible surface.
Wrapper guards loadbase and DTB before jumping to actual FD reset branch.
First hardware pass is experimental SEC/MMU/DXE test, not stable UEFI release.

FIRST REAL DXE RESULT (2026-10-24): capture evidence/raw/2026-10-24-degas-first-dxe-result.
SEC banner, MMU In and Loading DxeCore confirmed; core ran at443cf000.
No checkpoint/greenbar. Dispatcher left PcdDxe/GOP/Checkpoint discovered but unloaded;
DxeMain593 asserted NotFound after missing architectural protocols listing.
Inspected generated PcdDxe.depex and DegasGopDxe.depex: both depend on
13a3f0f6-264a-3ef0-f2e0-dec512342f34 (gEfiPcdProtocolGuid). Minimal FDF omitted
stock APRIORI. Add APRIORI PcdDxe only to resolve bootstrap circular dependency,
then ordinary GOP -> checkpoint depex. No hardware changes in this revision.

FIRST DXE v2: evidence/raw/2026-10-24-degas-first-dxe-v2-result.
PCD -> DegasGopDxe -> DegasCheckpointDxe loaded. Log confirms
DEGAS REAL DXE ENTRY and DXE GOP OK. User saw black rectangle at target.
Core then asserts missing remaining architectural protocols as expected for
incomplete checkpoint-only FV. This is not yet stable BDS/menu firmware.
Black rectangle candidate: GOP Reserved=0 versus proven framebuffer alpha=FF.
Next revision normalizes byte3 toFF within successful BLT destination rectangle,
leaving caller buffers, source-only readback and row padding untouched.
Android restored after v2; logs retained before any new probe.

FIRST DXE v3 SUCCESS: user confirmed green rectangle. Capture
 evidence/raw/2026-10-24-degas-first-dxe-v3-result, initial CRC0f048427 valid,
 whole console SHA2568fde2b9353d68c2f7261ab9d8bb5b6f1edeedc9c47732d53fd07f2b8636620dd.
 DEGAS REAL DXE ENTRY / DXE GOP OK confirmed. Stock boot_b restored, Android up.
 Alpha normalization fixes inherited OVL transparency interpretation.

Next bounded checkpoint adds CpuDxe, GICv3, TimerDxe and Metronome, not BDS yet.
 DT /interrupt-controller specifies GICD0c400000 size40000 and GICR0c440000
 size200000; replaced unused placeholder PCDs and mapped exact ranges.
 DT /timer PPIs13,14,11,10 -> INTIDs29,30,27,26, freq13000000.
 Checkpoint waits for timer/metronome protocols, counts 100ms EFI events for
 about3s and draws cyan progress under green GOP rectangle. Counter loop is
 independently bounded via counter-based MicroSecondDelay; no WaitForEvent hang.
 Hardware watchdog retained. This test must precede relying on timer-driven input.
 Button evidence: live DT gpio_keys/volup-key uses GPIO32 active-low (phandle20).
 Keypad node1c00e000; power/volume-down also have MT6363 PMIC paths, requiring
 separate mapping, not donor MT6358 code. No button hardware accesses added yet.

2026-10-25 TIMER HARDWARE SUCCESS: user saw cyan bar grow then stop.
Capture evidence/raw/2026-10-25-degas-dxe-timer-v1-result: GIC/Timer/Cpu drivers
loaded; DEGAS TIMER ARM Success; DEGAS TIMER EVENTS 37 GOP Success.
Console SHA2563880459991e01147ad05e50ca157c2f8f76afc3c206a1619cc2e3fed2d447b97.
Remaining missing architectural protocols expected (BDS/runtime/etc not added).
Stock boot_b restored and Android available. Live debug gpio confirms Volume_Up
GPIO294 with chipbase262 = pin32, input high ACTIVE LOW. getevent inventory:
event0 gpio_keys KEY_VOLUMEUP; event1 mtk-pmic-keys KEY_VOLUMEDOWN+KEY_POWER;
event2 mtk-kpd KEY_VOLUMEUP. Raw event capture running for user key sequence.

Button capture complete: events.txt has two GPIO event0 VOLUP down/up pairs,
two PMIC event1 VOLDOWN pairs and one POWER pair. getevent stopped after capture.
Source reference downloaded from MiCode/MTK_kernel_device_modules commit
3c4ccd07d5624879555ec710c11bb8818e5cb5c6 (duchamp, SAME MT6897 SoC, not degas
board configuration). drivers/pinctrl/mediatek/pinctrl-mt6897.c lines77-86
specifies DI pin32 at base0 +0x210 bit0. Degas live DT supplies base10005000,
pin32 active-low, live debug gpio confirms. Source stored evidence/sources/degas-buttons.
Source mt6363-registers.h TOPSTATUS=0x1e; PMIC keys driver reference saved for
later verified SPMI transport. No PMIC operations included in current firmware.

Button-selection test adds one GPIO MMIO page10005000..10006000, reads DIN210,
logs DIR010 and MODE340, no GPIO writes. After timer test, polls20s with debounce
(two agreeing samples after change at50ms polling). VOLUP cycles marker on three
preview rows; labels explicitly TEST ONLY NO BOOT ACTIONS. No Android chainload
or UEFI settings implemented yet. Watchdog retained for recovery.

INPUT v1 hardware success: capture2026-10-25-degas-menu-input-v1-result.
Timer37 events; GPIO32 read DIN10005210; ten press/release pairs and ten
selection changes logged, matches user report. DIR50081254 MODE00001001
DIN2C808249 at start. Thus input read works with inherited LK mode1 as well.
Stock boot_b restored, Android running.

PMIC route: DT mt6897-spmi, pmif1cc04000, master1cc01000, PMIF-P1cc06000;
MT6363 slave4. Android debugfs regmap0-04 range0..ffff with nine-byte lines.
Seek270, read9 returns exactly001e:1e (TOPSTATUS), no register writes.
Kernel regmap-debugfs source inspected: only complete requested lines read.
The reference MT6363 keys driver uses dedicated release IRQs, so its debounce
masks are not proof of actual bits for polling. Capturing TOPSTATUS against
separate volume-down and power holds before implementing UEFI key masks.

MT6363 TOPSTATUS observed: idle1e -> VOLDOWN held16 ->1e ->POWER held1c ->1e.
Thus active-low VOLDOWN bit3=8, POWER bit1=2 (not the reference IRQ-mode masks).
Capture pmic-status.txt/pmic-transitions.json retained under degas-buttons.
Prepared PMIF SWINF2 read-only transaction helper: DT base1cc04000, SID4,
mt6xxx offsets880/894/8a4/8a8, EXT_READ_LONG commandc400001e. Does not reset,
clear a busy foreign transaction, change PMIC registers, or retry after failure.
Checks init, waits idle, issues read, waits valid, reads, acknowledges, verifies
idle. Each phase capped10ms. Host fault-injection test passed all4 error paths,
command direction/address, no writes on busy/uninitialized, no stale output.
Menu v2 polls three keys, up moves previous, down next, power diagnostic confirmation
only. A transport error aborts test; no repeated failed command loop.

MENU INPUT v2 HARDWARE PASS: user all3 keys work. Capture
2026-10-25-degas-menu-input-v2-result: PMIC initial error0 status1e;
selection changes14, confirms8, error0. ConsoleSHA
 de7bd268ad9b94c08baf4ed46473c586757077cbb1d3348a53ee1eb91e130cca.
Android restored before next build.

Next experimental BDS/menu: add generic architectural runtime/security/variable
(emulated volatile backend)/capsule/monotonic/reset/RTC drivers. Checkpoint driver
now installs a real BDS architecture entry, menu invoked only after DxeCore checks
all architectural services. RTC remains inherited dummy clock, not wall-clock
support; persistent variables, storage, OS loading and standard console remain
unimplemented. No generic BdsDxe included (custom platform BDS instead).
Menu device info/display test/end test, no misleading Android action.
DT watchdog1c00a000 matches MT6897 watchdog source: restart offset8 key1971.
Map singlepage and reload existing watchdog once/second in healthy menu loop.
No mode/timeout changes. On read error or END TEST, stop feeds and wait reset.
Hardware validation pending: stay in menu >60s, select END TEST, returnfastboot.

BDS v1 pass: user reports >minute and responsive buttons. Log captured under
2026-10-25-degas-bds-menu-v1-result: REAL BDS ENTRY, no missing-architecture
assert, watchdog mode7d, menu exitSuccess after8 moves4confirmations.
Log ended at poll515 (not1200 heartbeat); exact70s duration is user-observed,
not independently established by this log. Android restored.

User requested black/white minimal UI. Wrapper and BDS now black, white title,
three text items and one status line; removed borders, color bars, preview text.
Wrapper build96c704795174f8f1a7e84aec265d4f869da74f635d5052d958610dabd6ca554b.
Next menu item UFS SNAPSHOT reads standard HCI registers only, version first;
unknown version stops. No UIC commands, DMA, reset, power or media writes.
Fresh Android DT confirms HCI112b0000 size3000; baseline-v2 has full liveDT,
three LUN first64KiB hashes, metadata. SKhynix HN8T174EJKX075, spec0400.
LUN0/1 boot4MiB; mainLUN2=sdc, 516620288 sectors of512bytes, logical4096.
Main primary GPT headerCRC verified against Android capture. Baseline-v1 failed
length check because adb exec-out merged dd stats; preserve failedcapture,
use baseline-v2 where dd stderr suppressed and all3 reads exactly65536bytes.

UFS survey v1: captured 2026-10-25-degas-ufs-survey-v1-result, console SHA256
7d964e9a16f5da65f684eab0c9f7b2dfb70850a1430091169e13f51412dd9759.
VER=400 CAP=55c7051f PMID=426. HCE/HCS/IE/AHIT/queue bases/doorbells all0.
Controller register interface accessible, HBA disabled; disk reads NOT proven.
Android restored. Same-SoC reference MiCode MTK_kernel_device_modules commit
3c4ccd07d5624879555ec710c11bb8818e5cb5c6 (duchamp, not degas board)
saved evidence/sources/degas-ufs/ufs-mediatek.{c,h}. Its HCE PRE_CHANGE
requires XOUFS_CTRL bit7 to prevent host hang; refclk has request/ack plus
secure-firmware notifications. Do not blindly enable HCE on this evidence.
Survey v2 adds read-only vendor registers 140/144/160/2100/2200/2240
to determine retained refclk/control/IP state before selecting enable sequence.

Survey v2 hardware PASS: console SHA 3f9b2bf059758106af516bb3a44f9a1fd588ad51a98b251b3e9a641e84d62669.
XOUFS=102 REFCLK=3 OPT=40 EXT=0 MPHYCTRL=7 IP=01440000.
Next experiment HCE only: exact VER/IP/refclk guards, HCE/IE/queues inactive,
set XOUFS bit7 per MediaTek PRE_CHANGE, HCE1, 600us then bounded100ms
poll HCE1 and UCRDY. No UIC/DMA/queue/media commands. HCE0 cleanup bounded
100ms; restore XOUFS only after HCE0. End menu session after experiment.
No full reset/power sequence claimed; this tests retained hardware state.

HCE v1 PASS: ready1 HCE1 HCS8 polls0 after600us. cleanup1 XO restored102.
Console cd2204de684f3b256c8c781fb0a976a184aa7cc624d335ee4b8017c429f95ec5.
Menu stopping intentional; user interpreted as freeze. Next UI explicitly says
TEST ENDED WAIT FOR RESTART. Next test local DME_GET attributes15a9/155e/d0a6/d0a8.
No DME_SET/link startup/DMA/media access. Each command100ms limit.

Attributes v1 PASS console e8c7208a5cbd534cb103d95074e1a3f28250d6ad2e291f4722a16ea2de2900e1.
Allresult0: 15a9=6,155e=0,d0a6=90,d0a8=0; cleanup1.
Next LINK STARTUP only: guard attributes LCC0/LPM0/deepstallbit6clear,
therefore no DME_SET needed. Command16 waits max1s; then device-present
max100ms. No DMA/SCSI/media reads or writes. HCE cleanup as previously.

Link v1 failed: UIC completion1 result1 polls996 (100us intervals), IS404.
Not host software timeout. Cleanup1. Console ea23bfd2c1df6bb8a2317fed7ecd5471ec02aaf57d4706b31e167c7d61c670e4.
Core ufshcd_link_startup retries with HBA enable; next test caps3 attempts with
HCE0/1 between, retaining verified clocks. Does not claim full SoC reset path.
Always captures PHY/DL/NET/TRANSPORT/DME errors BEFORE HCE reset and ack.
No DMA or media commands.

Link v2 all3 failed result1 ~99.5ms, HCS8 PA80000010; DL/NET/T/DME0.
Console6592d26d2b840e79b37b55b9c5f3a1fa08185587060fbaa0104ba47fd2feb66d.
Next v3 adds Android device reset before HCE: SMC64 fast SiP fn276 = c2000276,
Arg1 DEVICE_RESET bit1=2 Arg2 low0/high1; 10us low and10ms settle.
Source ufs-mediatek-sip.h and ufs_mtk_device_reset; actual Android logs device
reset done before link. HCE already0 required. Always release after assert call
even if error; stop on either nonzero return. No media commands.

Link v3 USER REPORT READY. Important: NEW RAM LOG NOT RECOVERED.
Result directory2026-10-25-degas-ufs-link-v3-result: pstore empty; debuglogger
console SHA6592d26d... equals v2 failed run, thus stale and NOT v3 evidence.
No claim of captured SMC/attempt/cleanup values. Repeating exact v3 image
for watchdog-preserved log before DMA. Stock Android restored successfully.

V3 repeat captured SUCCESS: console534bdfa4a7c402c002ca1013e5d4e8ad684f966388894e62c44ff20eedb9fad5.
SMC low/high a0=0, link attempt1 result0 polls179 HCSf devicepresent1 cleanup1.
Adding device-reset resolved observed startup failure (v2->v3).
Next NOP only: reserve uncached44470000..44480000 from loader-owned arena,
shrink DXE heap by64KiB. DT /soc dma-ranges identity0..64GiB, no host iommus
property and no Android host iommu_group. NOP OUT tag5a, response32bytes,
PRDT0/data0. Single slot, no retries,1s bound. Validate OCS/type/tag/response.
Disable HCE before clearing bases; never release reserved DMA memory on failure.

NOP v1 PASS console5cc3721403332a34db92e9d81ac24499455c1dcc92a1be5c595b261cded42d32.
NOP ok1 polls17 door0 IS5 OCS0 type20 tag5a resp0 cleanup1.
Next READ DESCRIPTOR IDN0, opcode1 stdread1, requested255, no PRDT.
Response offset128 capacity288, validate OCS/type36/tag5b/queryresult/
lengths/opcode/idn/index/selector and bounded descriptor length+ID.
Print manufacturer/spec/LUNs and descriptor bytes; no media data or writes.

IDENTIFY PASS console6334d40d5152b527544bf72ae1754ca2e327541984a290091d75a8f8645b966f.
Descriptor89bytes LUN3 WLUN4 spec0400 manufacturer01ad matchesAndroid.
Next set transient fDeviceInit/queryflag1 then waitclear max20 reads;
READ CAPACITY16 LUN2 only32bytes PRDT; retry max3 only validated unitattention.
Expected lastLBA03d95fff block4096 fromAndroid sdc512sectorcount516620288.
No blockwrite, format, trim, provisioning or configdescriptor writes.

Capacity v1: initflag set/read success, readcap transportOCS0 but resp1
SCSI2 flags0. Parser incorrectly rejected target-failure response before sense.
Sense cause not yet known. Fix acceptresp1 only to inspect CHECK CONDITION,
never as successful data; bound residual<=32/sense<=18 and retry onlykey6.
Print key/ASC/ASCQ. Regression test uses resp1+underflow residual32 onUA.
Console74e158822de046bfbd3498c509e4d7e8f1d8601403fbf46dade8b33f58a91972.

CAPACITY v2 PASS: consolecd5591822922ee23717e1bbacd10ceab2c2f023290c70d98a35903543ac1a978.
UA06/29/02 onfirstread, retry success. Last03d95fff block4096 LUN2.
Next READ10 LUN2 LBA0..15 one4KiBblock perrequest; perblockCRC32 against
freshAndroid first64KiB SHA c05d0667e8ec6c498fe9c90a7d1b987dd6a3a3a22a6a21cc6fe657d4222fb708
(equal priorbaseline). Also primaryGPT signature/headerCRC checked.
No write-CDB; transport onlyallow0/32/4096 with scopedcommandguards.

READ v1 hardware PASS (2026-10-25): all sixteen 4KiB blocks match Android;
primary GPT header CRC 5B443C42 matches. Console SHA256
e0f1e526a75054a658607d6535fba653bcfa8edb3417fb9792c314352fb2d2b5,
evidence/raw/2026-10-25-degas-ufs-read-v1-result. Cleanup=1.
Stock boot_b restored and Android available before next preparation.

Next: degas-ufs-blockio-v1. Temporary read-only EFI_BLOCK_IO LUN2,
4096-byte blocks, LastBlock03d95fff, bounce buffer in reserved uncached arena.
READ10 guard expanded from LBA0..15 to capacity bounds; no write CDB accepted.
DiskIoDxe and PartitionDxe added to FV. Non-recursive ConnectController,
94 GPT children expected from captured primary array, compare complete entry
and child media bounds/read-only flags. Disconnect/uninstall before HCE stop;
static callback state marked inactive even if disconnect fails.
No FAT/ESP creation or filesystem mounting yet. Host tests: capacity,
CRC read probe, real UEFI-type BlockIO boundary/error/write-protection cases.
Build passed; hardware partition enumeration remains untested.

BLOCKIO v1 hardware PASS: user GPT PARTITIONS MATCH ANDROID; captured console
0b18d55f2d52469dd14ee5e4867db91273e13075481098c2627c2778b20be8db at
evidence/raw/2026-10-25-degas-ufs-blockio-v1-result. All 94 children match,
19 BlockIO reads, zero attempted writes; install/connect/disconnect/uninstall
all Success and HCE cleanup=1. Stock boot_b restored, Android accessible.

Next degas-ufs-efi-v1: native FatPkg plus temporary read-only FAT slice located
inside experimental boot_b at +16MiB, length32MiB (absolute LUN2 LBA448512,
8192 blocks of4096). Whole flashed file48MiB remains inside64MiB boot_b.
No GPT changes or writes to linux_root/userdata. FAT created with dosfstools,
checked fsck.fat -n and EFI extracted via mcopy for byte-for-byte comparison.
File PROBE.EFI is a separately built ARM64 UEFI application, not in FV.
Menu opens file, checks MZ, LoadImage from its filesystem device path (no RAM
source buffer), checks LoadedImage DeviceHandle, then StartImage with request/
reply mailbox. App only logs entry and returns a magic reply. Both filesystem
and root BlockIO deny writes; disconnect before HCE cleanup. Tests include
slice bounds and error propagation. No Windows loader/ESP is installed yet.
Firmware artifact boot.img SHA256
b0b5d11cde002b4ca0427de22416bd6d065e2fc32f4475fd61f761c59d8da84f.
Test pending hardware; expected menu result EFI APPLICATION RAN.

EFI v1 REGRESSION: user reports boot loop, no UEFI menu. Found device in
fastboot, product degas slot b; restored verified stock boot_b and Android.
Capture evidence/raw/2026-10-25-degas-ufs-efi-v1-failure has NO fresh pstore
console. debuglogger SHA0b18d55... is the previous BLOCKIO v1 success, not
evidence about this failure. Exact cause unknown.
Next isolated control: flash the identical boot-uefi.img prefix WITHOUT the
FAT overlay (266240 bytes, SHA f907a8e8b7c5165ab211a8164db065014d22d6c418e67e442222c01e6fe5fde5).
No source changes for this comparison. Only test menu and END TEST, not UFS.
Separate static finding for future file test: EnglishDxe UnicodeCollation2 is
listed in common DSC but missing from FDF; Fat requires it when binding a disk.
This is not evidence of the pre-menu boot-loop cause; defer until comparison.

NO-OVERLAY CONTROL PASS: user confirms menu works. Console SHA256
def72a832d4aeb17a6e5acfdd586709d25586ad5bd468ccd1abb966ce4555a20 at
evidence/raw/2026-10-25-degas-ufs-efi-v1-no-overlay-result explicitly shows
Fat.efi loaded, real BDS/menu, END TEST and successful exit. Same firmware
as failing overlay, so FAT driver inclusion alone is not sufficient to cause
the observed loop.

Concrete packaging defect found: stock AVB footer at67108800 references
vbmeta_offset=16568320, vbmeta_size=1664, original_image_size=16568320.
Original FAT-overlay packager padded boot-uefi.img to16MiB with zeros,
destroying AVB0 at16568320. Its48MiB output also omitted the stock footer.
Whether short fastboot flashes preserve trailing bytes on this device was
not read back in this comparison, so exact bootloader failure branch remains
unproven. This defect is the leading cause, not a claim of captured LK error.

Corrected packager uses exact hash-verified64MiB stock template; overlays only
the verified custom Android boot prefix and known-zero16..48MiB interval.
Stock vbmeta+footer and every byte outside these two overlays stay identical.
This preserves metadata structure, NOT stock cryptographic validity for the
modified payload (unlocked diagnostics as before). Regression test demonstrates
old AVB destruction and new preservation. Add EnglishDxe UnicodeCollation2 to
FV for forthcoming FAT binding. Next hardware test artifact degas-ufs-efi-v2.

EFI v2 boots, test FAILED. Fresh console SHA256
9372645b616ad7e12c5591b6986e677853a74190436f70348cc5cce904019d87 at
evidence/raw/2026-10-25-degas-ufs-efi-v2-result. GPT94/94 PASS; FAT mounted,
file open/read Success; LoadImage=Access Denied. No writes or I/O errors;
detach/uninstall and HCE cleanup all success. Restored stock and Android.

Cause of EFI v2 LoadImage denial: custom MenuBdsEntry never signals EndOfDxe.
SecurityStub.c calls Defer3rdPartyImageLoad for non-null file buffers; in
Defer3rdPartyImageLoad.c mEndOfDxe starts FALSE and disk images return
EFI_ACCESS_DENIED until the EndOfDxe callback sets it. No cryptographic
verification handler is linked with ENABLE_SECUREBOOT=0, but this separate
phase gate is always active. Fix BDS lifecycle, not the security handler:
signal EndOfDxe and immediately publish DxeSmmReadyToLock before menu and
any external LoadImage. Unit test checks event/lock ordering and propagation
of setup errors. Next artifact degas-ufs-efi-v3; hardware outcome pending.

EFI v3 HARDWARE PASS: user EFI APPLICATION RAN. Fresh console SHA256
55489c4700d947a38684faf5e1eec6bff2dcb73c108138755a518941990ccb96,
evidence/raw/2026-10-25-degas-ufs-efi-v3-result. EndOfDxe/ReadyToLock success;
FAT mount and file read success; LoadImage success; application entry from UFS;
StartImage success with reply31504C5246474544; detach/uninstall success,
blocked_writes0, HCE cleanup1. Boot image SHA
7bd0681ae5c843321210b2639266a88e8eb09a55b29a52a576abcdfc9c63d736.
Stock boot_b restored after capture preparation; Android accessible.

Windows readiness at this checkpoint: EFI launch is proven, not OS handoff.
Current conventional memory is only ~64MiB DXE heap; no degas ACPI tables
published in FV; test FAT is32MiB inside boot_b; tests intentionally stop UFS
and wait for watchdog, not a persistent OS boot path. GOP still requires a
DSI refresh when Blt updates the retained command-mode display, and alpha
is forced by Blt; arbitrary Windows framebuffer writes after EBS are unproven.
Before meaningful WinPE test: reconcile larger usable RAM, publish minimal
ARM64 ACPI from degas topology/timer/GIC, define watchdog/EBS/display handoff,
and provide appropriately sized storage for pristine ARM64 PE. A Windows
MediaTek UFS driver is a separate later task; UEFI reads alone do not supply it.

2026-10-25 offline continuation while user has the phone: no device access,
flashing or reboot. Added tools/prepare_degas_windows_facts.py and five host
tests. Fresh FDT plus conservative historical LK/memblock exclusions yields
32 additional page-aligned candidate RAM intervals, 10654.23 MiB. NOT enabled
in MemoryMapLib: four dynamic reservations and current handoff ownership remain
unresolved. Report and checksum-valid 104-byte GTDT draft are under
artifacts/degas-windows-preflight. CPU affinities come from reg, not unit
addresses: cpu@100 is MPIDR0x400 and cpu@200 is0x700. GICD0c400000,
GICR region0c440000+200000, maintenance INTID25; timer29/30/27/26, DT13MHz.
No per-CPU redistributor bases or secondary-CPU availability are invented.
Checkpoint BDS now logs boot MPIDR/MIDR/CNTFRQ and first GICR_TYPER (read-only,
same mapped frame read by existing GIC discovery), then proceeds normally.
Runtime outcome pending; draft GTDT is not installed in firmware.

Phone returned during offline preparation. Root capture saved to
evidence/raw/2026-10-25-degas-windows-preflight (capture.json hashes).
Fresh FDT fixed reservations and CPU/GIC/timer facts match the preceding capture;
123 current memblock reservations saved. Intersecting with these additional
exclusions leaves11171766272 bytes as candidates, still NOT installed.
Stock boot_b SHA matched fa018193... before reboot. Firmware build succeeded
in1m55s; five new host tests passed. Artifacts/degas-platform-facts-v1/boot.img
SHA25658ab14af170abbb5a7ecda7cc1fbc98bd418bf5b7ee6f8ff6abb5e50e732f5d2,
64MiB, preserves stock AVB metadata and proven FAT probe. Verified fastboot
product=degas slot=b, flash boot_b and reboot both OKAY. User asked END TEST,
then Volume Down to fastboot. Next: restore verified stock boot_b, reboot
Android and capture console-ramoops-0 before another firmware attempt.

Platform-facts-v1 result captured after restoring verified stock boot_b:
evidence/raw/2026-10-25-degas-platform-facts-v1-result/console-ramoops-0
SHA2561238585d0d40b83488a03576e62040c16b589c2f54716463f55e48b29ee4e82b.
Boot MPIDR=81000000 (affinity0; MT/U bits must NOT enter MADT), MIDR411FD461,
CNTFRQ13000000. First GICR0c440000 TYPER20 affinity0 boot_match1. EndOfDxe
and menu exit success. Single-CPU ACPI now generated by generate_degas_acpi.py:
DSDT70,FADT276,MADT148,GTDT104 bytes. No secondary cores, peripheral resources,
sleep/reset register guesses or GICR discovery range. Runtime installer guards
CPU/timer identity, queries PSCI_VERSION only, validates source tables and the
published RSDP/XSDT/FADT->DSDT chain, and rolls back on errors.
IASL0 errors/warnings,3 Python evidence tests and independent EDK2 structure
decode passed. First compile failed from missing BaseMemoryLib header for
CompareGuid; fixed. Artifacts/degas-acpi-v1/DO-NOT-FLASH-stale-before-build.img
contains the old FD and MUST NOT be flashed (never was). Packager now rejects
any latest build log without Done or containing Failed. Next valid boot image
will be under degas-acpi-v2 after successful compilation. Phone still Android.

Corrected ACPI build completed successfully (2m15s); FD SHA
f58ed047a90ebe2518d46f9b723ec04bf28d0a3d13eae274546455e1c369d581.
Packaged artifacts/degas-acpi-v2/boot.img SHA
12bf673f1b5a6aeb48560692dcaf1e814b86ea5eb2ae7ef9c2e35dc2cec39153.
Verified fastboot product degas/slot b; flash boot_b and reboot successful.
Await user END TEST -> fastboot. Next restore stock boot_b, boot Android,
collect fresh pstore and check DEGAS ACPI PSCI_VERSION/READY plus published
RSDP/XSDT/DSDT/table checksums. Windows readiness still unproven; RAM unchanged.

ACPI-v2 HARDWARE PASS: restored stock boot_b and captured
evidence/raw/2026-10-25-degas-acpi-v2-result/console-ramoops-0 SHA256
b4e07812d4e98ba35b83da9cd8816e1208f97ed278b02fd9a6b4d9efb114ad43.
PSCI_VERSION=00010001, all four tables published with valid checksums;
RSDP4406A018 XSDT4406AF18 entries3 mask7, DSDT4406AB18 linked by FADT.
DEGAS ACPI READY cpu1 uid0 mpidr0 gicr0c440000 timer13000000, menu exit success.

Next diagnostic: RESERVED mapping [140000000,180000000), 1GiB, unchanged low
heap. check_degas_ram_probe_range.py verifies containment in current candidate
RAM and disjointness from complete alloc-ranges of ALL four dynamic DT nodes,
not just their historical placements. New RAM ACCESS TEST samples513 pages:
one4KiB page per2MiB plus the last4KiB page. Saves all originals (~2MiB) in the
existing low heap, writes address-derived patterns to all samples simultaneously,
checks, repeats complement, restores and verifies originals. CPU cache clean/
invalidate plus DSB surrounds physical accesses. Watchdog fed per page. Sync
external abort remains unrecoverable; watchdog then fastboot recovery required.
Host model verified success, injected corruption, address aliases and failed
restoration. Sparse PASS must not be described as exhaustive1GiB validation.
Menu preserves UFS EFI LOAD TEST and adds RAM ACCESS TEST; END TEST is row4.

RAM-access-v1 build success2m17s. Boot image
artifacts/degas-ram-access-v1/boot.img SHA256
ffa97b69a68a7b812b48f88c5a5abb9d6d241562527033fa779f5a148c539c42.
Host boundary/alias/restore model passed and candidate range audit passed.
Verified product degas slot b, flash boot_b and reboot OKAY. Waiting for user
RAM ACCESS TEST -> result -> watchdog reset -> Volume Down fastboot. Next
restore verified stock boot_b and collect fresh RAM log. Important: region
140000000..180000000 remains MEM_RES/Reserv, NOT Conv. No full OS or enlarged
allocator test yet; UFS write support remains blocked in UEFI.

RAM-access-v1 HARDWARE PASS: user reports RAM SAMPLES PASS RESTORED. Restored
verified stock boot_b, captured evidence/raw/2026-10-25-degas-ram-access-v1-result.
console SHA256c24dce87910c5dccd536d2a7b12a7b83d6f84ac0e9b66b245da7b080595c8d68.
513 pages/2101248 bytes checked across1GiB; phase6 page512 at17FFFF000;
result0 restored1 failed_phase0, ACPI READY and menu exit success.

Next allocator test: publish [140000000,180000000) as SYS_MEM/Conv/WRITE_BACK.
Low DXE heap remains unchanged and UFS DMA remains reserved at44470000.
Replace sparse RAM ACCESS TEST with RAM ALLOCATION TEST. It records actual
GetMemoryMap conventional/window-free totals, allocates256MiB via
AllocateMaxAddress, rejects allocations not wholly inside the selected1GiB,
writes/checks ALL allocated words with two address-derived complementary
patterns, performs cache maintenance per1MiB, and frees only allocated pages.
Host tests cover boundary/overflow rejection and complementary patterns.
Publication/large allocation still pending hardware result.

RAM-allocator-v1 build succeeded2m50s (Checkpoint.obj newer than final included
RamAllocationCore.h). Boot image artifacts/degas-ram-allocator-v1/boot.img
SHA256a7df2d3ee33967b9f4e77e7a82960ca09509cb66b038e2608b77d876592970d5.
FD SHA2563bf1955e899b6c79dfd0f43979bfbbbaadd54a7284e3679b894db5a7d46ffd99.
Verified fastboot degas/slot b, flashed boot_b and rebooted OKAY. Await user
RAM ALLOCATION TEST result then fastboot via watchdog/Volume Down. Next restore
stock boot_b, collect pstore, inspect actual conventional memory totals,
allocated high address, two full256MiB verification passes and FreePages result.

RAM-allocator-v1 HARDWARE PASS. Stock boot_b restored; result captured at
evidence/raw/2026-10-25-degas-ram-allocator-v1-result/console-ramoops-0,
SHA256900e3fd738904a2ee7dc6f53cbbd5d5e985f6500cb2eded24a8fd3ce0de61f8d.
Conventional1133924352 bytes, new window free1073741824. AllocatePages Success
base170000000 size268435456, both full write/verify passes completed, FreePages
Success. ACPI READY and menu exit success.

WinPE storage read-only inspection: linux_root resolves sdc94, size144611180544,
ext4 label degas_uefi_lab UUID8b12a105-f220-4a6d-a7da-1e8584f79290. Mounted
temporarily ro,noload under /data/local/tmp/degas-winpe-inspect-20261025; root
contains only lost+found, df used24KiB. Unmounted and temporary directory removed.
Evidence evidence/raw/2026-10-25-degas-winpe-storage/root-listing.txt. No format
or GPT writes. Plan artifacts/degas-winpe-preparation/storage-plan.json records
candidate replacement with FAT32, subject to user decision. Original ARM64
bootmgfw and pristine647695987-byte WIM verified (2490c9c9...); do not use OP12
driver-modified WIMs. Windows-host BCDEdit read of source BCD denied access;
need offline BCD preparation. Phone remains Android. Remaining: storage choice,
degas BCD, persistent UFS filesystem path, watchdog/EBS/command-mode GOP handoff.

## 2026-10-25: allocator PASS and authorized Windows storage layout

The 256 MiB allocation test passed two full write/read patterns and freed the allocation. Captured `degas-ram-allocator-v1-result/console-ramoops-0`: 1,133,924,352 bytes conventional total, 1 GiB in the added window. No Windows boot claim yet.

User explicitly authorized using/repartitioning the empty linux_root. The original primary/backup GPT and prepared/readback GPT are in `evidence/raw/2026-10-25-degas-windows-partition-plan`. Both copies were verified byte-for-byte after writes; all 93 original non-linux_root entries remain identical. Android was rebooted and its new nodes/starts/sizes verified.

- sdc94 / esp: LBA 29267968..29792255, 2 GiB, FAT32 / 4096-byte sectors and clusters.
- sdc95 / msr: LBA 29792256..29796351, 16 MiB.
- sdc96 / Windows: LBA 29796352..64573431, 142446919680 bytes, not yet formatted.

ESP now contains 59 original ISO boot files plus pristine sources/boot.wim. All 60 files SHA-256 verified on-device, sync and unmount completed. No OP12 drivers or installer scripts injected. Existing Android/userdata entries untouched. Never rerun the partition --apply script on the new layout.

Next: first read-only WBM launch through the actual ESP child, with a Boot Services timer feeding the existing watchdog and RAM log on ExitBootServices. Hardware watchdog is intentionally retained for bounded recovery; this is not a stable unattended Windows build.

First WBM test built and flashed to boot_b: `artifacts/degas-winpe-first-v1/boot.img`, SHA256 `548def4caf49c841936466817015b3f901b5904fc0e74ce8c43c57008cfae6e2`. Fastboot product degas / slot b checked, flash and reboot both succeeded. Stock AVB/padding preserved. Added HiiDatabase/GraphicsConsole/ConSplitter for real firmware console; RAM EBS marker and watchdog timer. Awaiting user selecting WINDOWS BOOT MANAGER and returning fastboot. No successful WinPE boot claimed. Current boot_b is experimental, stock recovery reference unchanged.

## First WBM attempt: visible menu froze, no retained UEFI log

User saw unchanged menu after selecting WBM and manually held Power + VolumeDown; no automatic reset observed. Stock boot_b restored and Android booted. Capture `evidence/raw/2026-10-25-degas-winpe-first-v1-result`: pstore empty; debuglogger console is stale Android shutdown from 12:01, SHA256 c7a45c139ea50093fe41dc2eaeb4de03db4cd141692f848ea7add5cff9408784. No UEFI milestone can be inferred from that file. Current dmesg confirms init could not copy console-ramoops-0. Do not claim UFS/LoadImage/StartImage success from this run.

Found a diagnostic limitation: v1 periodic Boot Services timer reloads hardware watchdog indefinitely, so a WBM wait/error can remain alive without automatic reset. Next v2 adds visible stages through UFS/console/ESP/LoadImage/StartImage and a 120-second budget for BOTH periodic timer and BlockIO reloads. Then existing hardware timeout (~40s) should recover if watchdog remains enabled. This is instrumentation, not a proven boot fix. No disk layout/data changes for v2.

User steered next build to adapting lancelot boot foundation. See `docs/DEGAS-LANCELOT-BOOT-FOUNDATION-20261025.md`. V2 stage-only image was NOT flashed. V3 compiled successfully (2:21), host handoff test PASS, packaged AVB-preserving 64 MiB boot image and flashed boot_b after product degas/slot b/hash verification, then rebooted. Image `artifacts/degas-lancelot-boot-v3/boot.img`, SHA256 be3b15de48a30f0d440b27e68820039a080193acc6568d584afbc679d114e00c. Current phone state: booting experimental UEFI, awaiting user WBM test. Android stock restore reference unchanged. No WinPE success claimed. Unlike v1, v3 disables hardware watchdog at successful EBS; pre-EBS feeds expire after120seconds then hardware reset about40seconds later. User should photograph last stage; manual reset only after3minutes if no recovery.

## v3 hardware result: exact pre-Windows ASSERT identified

Automatic watchdog recovery preserved fresh UEFI log. `evidence/raw/2026-10-25-degas-lancelot-boot-v3-result/console-ramoops-0` SHA256 `3a844fb731437e62c1099841e45430aec43abb78089331b4ff2b2e9156e90e7b` equals Android debuglogger copy. UFS verified 96 GPT children, graphics console and ESP filesystem success. Final lines: `DEGAS WINDOWS EfiBootManagerBoot begin`, VariablePolicy lock, `ASSERT_EFI_ERROR (Status = Not Found)`, `BmDriverHealth.c(551)`.

Source BmRepairAllControllers locates gEfiFormBrowser2ProtocolGuid unconditionally when PcdDriverHealthConfigureForm is nonzero. Missing SetupBrowserDxe in our degas FDF caused this exact stop BEFORE loading/starting Windows. Lancelot includes it indirectly via MediaTekPkg/Extra.fdf.inc -> SiliciumPkg/Extra.fdf.inc. Added exact same SetupBrowserDxe INF. Its HiiDatabase/ConfigRouting dependencies are already included; optional FormDisplayEngine notification does not prevent installing FormBrowser2. No need to disable ASSERT or bypass driver health policy. This is a fix for observed v3 firmware stop, not proof of all subsequent Windows prerequisites.

Stock Android restored; next v4 build underway, no new partition or data writes.

V4 built successfully and flashed boot_b, then rebooted. Image `artifacts/degas-lancelot-boot-v4/boot.img` SHA256 e487fb63c74fbb8e66579c7b72fdf2214e225a7ab5d58de21d1f6c093d074e47. Single firmware fix vs v3: include SetupBrowserDxe for FormBrowser2; no bypass of ASSERT or driver health. Product/slot/hash checked. Awaiting user WBM test. Note future performance concern: current UFS reads are still single4KiB commands, observed94polls at100us in v3. Loading full647MB WIM may exceed120s boot watchdog feed budget; do not interpret a future budget reset during successful reads as the same ASSERT. First determine next loader milestone from log, then consider bounded multi-block transfers within fixed64KiB DMA arena.

## v4 hardware result: active reads, trace ring flooded

After automatic watchdog reset, restored stock boot_b and captured logs. Initial capture was interrupted by transient ADB disconnect during Android boot; retry succeeded. `evidence/raw/2026-10-25-degas-lancelot-boot-v4-result-retry/console-ramoops-0` SHA256 7a81f19a8980c9bb1b92efc704012a50fceaf7675f155e595e62ee0976be47d7, same as debuglogger. Entire262132byte ring contains3048complete UFS success records, nofailedpackets, polls93..94. Initial boot/ASSERT/loader milestones were overwritten, so exact file/Windows entry is not established. Nonetheless firmware was actively issuing successful reads up to end, unlike v3 assertion. Poll interval100us, single4096bytecommand: roughly9.3..9.4ms completion wait, throughput upper bound0.42MiB/s before otheroverhead. 647695987byteWIM cannot load in120sec on that path. Budget watchdog reset is consistent with ongoing slowI/O, not proof of Windows hang.

V5 pending: READ10 up to14blocks/57344bytes inexisting DMA [44472000,44480000), singlePRDT, no new allocation, no writes; first64KiB reference probe stillvalidates4KiBreads, nativeGPT partition driver CRC-checks multiblock reads. Commands validate length/LUN/opcode/count/endLBA. Packet logs first16, every1024, and all errors; BlockIO summary every16MiB. Boot feed budget300sec, watchdog disableonlyatEBSunchanged. Host test test_degas_ufs_read_command.c PASS including DMAend,lastLBA,zero/oversized/wrongopcode/LUN/length rejection. Build underway. No v5hardwareclaim.

V5 built successfully and flashed boot_b after degas/slot b/hash verification; flash and reboot OKAY. Image artifacts/degas-winpe-ufs-batch-v5/boot.img SHA256 daeb3a8f7a2327832b4d55788e511bed0d9f399cc69ec87051bd60582733c47f. Await hardware WBM test with up to six minutes for pre-EBS watchdog recovery. Read batching and reduced logging are not yet hardware validated; no Windows entry or throughput claim. Stock restore reference unchanged.

## V5 hardware result: bootmgfw loaded, transfer rate still low
User waited for automatic reset and returned fastboot. Restored verified stock boot_b, rebooted Android. First capture failed during transient ADB disconnect; retry complete at evidence/raw/2026-10-25-degas-ufs-batch-v5-result-retry. console-ramoops-0 22028 bytes SHA256 90ae046a5c79ddfa490ac6b7153eba5f8540a304cc3cd4670647a05e66dc682a equals debuglogger copy.
Log retains beginning: 96 partitions, console/ESP success, [Bds]Booting Windows Boot Manager, Loading driver at 0x42FE2000 EntryPoint=0x4301C130 bootmgfw.efi. No ASSERT or EBS marker. ConvertPages preferred-range failures appear before successful image relocation; do not label fatal without further evidence.
Last records successful 57344-byte packets count1024/2048, polls1035/1034 at100us. Batching is accepted but transfer completion scales with bytes: approx0.53MiB/sec upper bound excluding overhead; a647695987byte WIM would need about20minutes at this rate. Actual file identity and total completed bytes unknown: summary occurs only when whole BlockIO request returns, so a large in-flight read suppresses summaries. Watchdog budget reset consistent with continued I/O. Previous assertion is absent; Windows kernel entry not established.
Code UfsLinkAttempts only starts link, no explicit gear/power-mode negotiation. Low initial link rate is a candidate, not yet measured. Next inspect/read PA power/gear/lane attributes and add progress INSIDE chunk loop; do not blindly increase timeout or use unverified HS PHY settings. Phone now Android, stock boot_b. No new image flashed after v5.

## V6 HS negotiation implementation
User requested ordinary UFS speed. Android read-only inventory saved under evidence/raw/2026-10-25-degas-ufs-speed. Live DT includes mediatek,ufs-pmc-via-fastauto; device model HN8T174EJKX075; no reported PA/DL/fatal errors in Android show_hba. Android sysfs does not expose current UniPro gear in the inspected files; do not claim measured Android gear.
Added UfsHighSpeed.h using local vendor references ufs_mtk_pre_pwr_change/ufs_mtk_adjust_sync_length and ufshcd_change_power_mode. Read local+peer max RX HS gears and connected lanes, clamp host to G5/2lanes. Before rate B, minimum sync length0x48 at both ends for G1..G5; G1 FASTAUTO44 with noadapt, then negotiated FAST11 with initial adaptation for TXG4+ and local version>=5 (UniPro1.8 enum, NOT0x180). Bounded UIC waits; require command result0, power interrupt0x10 and UPMCRS=PWR_LOCAL1, readback final gears/lanes/mode/rate. Any failed access/transition stops Windows path, no opportunistic fallback on partially configured link. No raw block write capability introduced. First64KiB CRC and nativeGPT verification remain mandatory before WBM.
Host mock test test_degas_ufs_high_speed.c passes sequencing, negotiated/asymmetric gear limits, peer sync setting, failed attribute, missing power completion, invalid UPMCRS, busy transfer queue, noHS support and G3 noadapt. Hardware speed and transition still unverified. Added progress inside BlockIO chunk loop to retain evidence during large WIM read. V5 batching/read-only DMA policy unchanged. Building v6; phone remains stock Android until verified packaging.

User needs phone and explicitly prohibited reboot/flash. No reboot/flash command issued after that request. Final Android capture verified boot_b stock SHA fa01819342dd24164dea568f88f9715bc3132f738a68c49453907982946c1113 and boot_completed1, slotb. Saved UFS descriptor and DT clock properties. Reading /sys/kernel/debug/clk/clk_summary then returned empty output, subsequent fdt read failed device not found; user confirms unexpected reboot. Suspect clock debug read triggered kernel failure, NOT proven without pstore. Stop all device accesses now; do not repeat clk_summary. Previous full liveFDT available in baseline-v2 (491942 bytes), no reason to repeat. User informed and apology given. Need only after user returns/authorizes collect pstore to establish reason; do not claim reboot cause confirmed.
V6 build passed 2:13, host HS tests passed, packaged artifacts/degas-winpe-ufs-hs-v6/boot.img. NOT FLASHED. No hardware validation yet. Phone last written image is stock, verified before incident. Continue offline only until user returns.

## Offline follow-up after user took phone: V7
No ADB/fastboot commands after the unexpected restart. Read MiCode ufshcd-pltfrm.c pinned to 11f638af362bd6600a47fcb8e4739607fb118d36; saved source+hash. Corrected V6 gear negotiation to Android's common min(localRX, peerRX, hostG5) for both RX/TX. V6 was never flashed and is superseded. Added device-fatal and link-lost bits to HS and packet abort masks; stale UIC line-reset bit0x4 is not treated as a fresh fatal error.
Added UfsSpeedCheck.h: after basic reference CRC, re-read first64KiB sixteen times as 14+2 block commands, check every4096byte CRC against Android references, reject response/residual errors. Report transfer-only elapsed and KiB/s, explicitly REPEATED SECTORS (not cold-media benchmark). Windows remains gated on all checks.
Removed per-command zero of entire64KiB uncached arena: clear only32byteUTRD plus0x120byte UCD (request,response,PRDT). Data accepted only after full transfer. Host mapped-memory boundary test verifies response/status clearing, PRDT length/address, untouched payload and all memory outside descriptors. Host HS tests extended for missing UIC completion, fatal/link-lost, bad final readback and sharedgear/clamp. Both tests pass. V7 building; hardware transition and speed still unproven. Do not flash while user away.
V7 build successful (2:08). Packaged AVB-tail-preserving artifacts/degas-winpe-ufs-hs-v7/boot.img SHA256 f3d203111ba77811340406316fb55d592e8b14417b1ce5f51ff4c7ddadf4db08, 67108864 bytes. FD SHA256 c4bb79dc46b88039d497072fa30ce4439cee0c8fcf4a0b98cd2ba2d434b26a61. NOT FLASHED. Await phone return and renewed hardware availability. Next flash only boot_b after identity/hash checks, select WINDOWS BOOT MANAGER; watch UFS HIGH SPEED / UFS SPEED VERIFY / Windows stage. Capture complete automatic-reset log; inspect negotiatedmode, commongear, powercompletion, 1MiB repeatedCRC benchmark and ongoingread progress. Pre-EBS watchdog budget remains300seconds, EBS disableswatchdog. No successful HighSpeed or Windows entry claim yet.

User returned and authorized test. V7 image SHA256 f3d203111ba77811340406316fb55d592e8b14417b1ce5f51ff4c7ddadf4db08 verified; Android and fastboot identity degas/slot b confirmed. Flash boot_b and reboot both OKAY. Await user WINDOWS BOOT MANAGER test. Hardware HS result pending; stock restore reference unchanged.

## V7 manual reset: fresh RAM log lost
User reported brief UFS HS message then persistent START MU BOOT MANAGER, no automatic recovery, eventually manual Power+VolumeDown to fastboot. Verified degas/slotb/stockhash, restored boot_b and rebooted Android. Capture evidence/raw/2026-10-25-degas-ufs-hs-v7-result: pstore empty, console missing; debuglogger copy262132bytes SHA ab1aff4744aafc222605b3f214c83c5792d0baf62a8bc6581f0e80257f0e86d9 timestamp13:11 is old Android text, not V7. No throughput, negotiated gear, EBS or Windows-kernel-entry evidence recovered. Do NOT infer HS PASS from user recollection of brief label. Phone now stock Android.
Next isolate HS+CRC benchmark from WBM in a short test with automatic reset, then fix console visibility: clear custom menu and verify standard TextOut rendering before boot, examine command-mode refresh. User explicitly requested shorter watchdog. Do not repeat six-minute blind wait. Missing reset may indicate EBS watchdog disable but not proven. Windows UFS port is separate: UFSHCI common command structures reusable, MediaTek clocks/reset/PHY/power/DMA/ACPI/interrupts need distinct implementation, not address substitution.

## V8 standalone speed and console tests, shorter recovery
User requested separate UFS SPEED TEST, END reset about10sec, WBM maximum2minutes. Menu now six entries: info, RAM, WBM, UFS SPEED TEST, CONSOLE TEST, END. SpeedOnly uses same initialization/HS/CRC benchmark then stops HCI without installing partitions or entering Windows. Automatic END reset programs LENGTH=(10<<11)|8 and reset-only MODE (preserve other low24bits, clearIRQ/DUAL bits3/6,setEN,key22). WBM arms same10sec hardwaretimeout and feed cutoff60seconds (~70seconds preEBS). EBS stilldisableswatchdog; no guarantee afterEBS hangs. Host test exact WDT registerencoding and feedboundary passed.
Reviewed photographed Synchronous Exception: shared DefaultExceptionHandlerLib logs RAM then attempts ConOut OutputString after context dump; recursive fault during dump can prevent visibleoutput. GraphicsConsole uses GOP Blt, DegasGop invokes actual DSI refresh on successful drawing. No menuoverlay layer. Direct framebuffer writes do NOT invoke refresh and remain a separate unresolved Windows display risk. Added harmless CONSOLE TEST (standard OutputString, no induced CPU fault). WBM clears custommenu and writes startmessage via actualConOut. Await hardwarevisibility test; codepath existence is not proof.

V8 built and flashed boot_b after degas/slotb/hash checks. Image artifacts/degas-ufs-speed-console-v8/boot.img SHA256 4e826000bab8dca7bda95e9e3942c259567b675949ca138d015064b272cdfd44. Flash/reboot OKAY. Await standalone UFS SPEED TEST then automatic10second reset to fastboot. Console test present but not yet hardware verified. Build2:20; watchdog hosttest PASS.

## V8 standalone HS hardware PASS
User UFS SPEED AND CRC PASS, automatic reset to fastboot. Restored stock Android, fresh complete capture evidence/raw/2026-10-25-degas-ufs-speed-v8-result/console-ramoops-0 20754bytes SHA583e17211e8dd19a2dfa9319a68ebc062aab023cf183adab22f614c94fc58edb equalsdebuglogger.
HS negotiation confirmed PA_PWRMODE11, RX/TXgear5, activeRX/TXlanes2, HSseries2(B), localUniProversion6. Intermediate44 and final11 power completion UPMCRS1. All first64KiB CRC and GPT header match. Repeated64KiB x16=1048576bytes CRCpass, transport_us5170, transport_KiB_s198036 (~193.39MiB/s); repeated sectors / excludesCRC and applicationcopy so NOT sustainedmedia or WIMload benchmark. 4KiB commands polls1..2 vs93..94 inPWM. HCIcleanup1. END WDT mode35 length5000 readback (writekey8 notretained), reset10seconds. HighSpeed verified but Windowsboot and consolevisibility remainunknown.
Next same V8 image CONSOLE TEST; no firmware edits required.

Same verified V8 reflashed boot_b and rebooted OKAY for CONSOLE TEST, product/slot/hash confirmed. Await visible standard console and automatic10second reset. Current boot_b experimental V8.

V8 CONSOLE TEST hardware visible: user saw UEFI STANDARD CONSOLE TEST and This is not a CPU exception, automatic reset. Capture evidence/raw/2026-10-25-degas-console-v8-result/console-ramoops-0 6164bytes SHA256 3d136b2380b637ee0f5c378184cd6e8677876c2123fb3080d25c12bd192914b7 equalsdebuglogger. Standard ConOut->GOP refresh works; not proof of Windows directframebuffer rendering or exceptionpath underfault. Restored stockAndroid for capture. Next WBM sameV8, shortpreEBSwatchdog.

V8 reflashed and rebooted OKAY for WBM test after consolePASS. Product/slot/hash verified. Await Windows result; preEBS recovery~70sec, EBS disablesWDT. Do not confuse no-reset with provenOSentry.

## V8 WBM automatic reset: ongoing large read, CPU copy bottleneck
Captured fresh RAM evidence/raw/2026-10-25-degas-windows-v8-result/console-ramoops-0 30024bytes SHAc454db835334d197db209b1f6ef9af38e1f5a62b3fdbd75eea27dab7a8e13073 equalsdebuglogger. No ASSERT/exception/EBS marker. Bootmgfw loaded, repeated successful57344byteDMA (polls1). Large BlockIO request progress reaches403247104 cumulative bytes at65666816us, still255680512 bytes remaining. ~16MiB every2.815sec =5.69MiB/s applicationpath despite193MiB/s transport-only test. WDT reset during activeI/O. Fileidentity not explicitlylogged; size consistent withWIM but do not claim verifiedfilename.
Found byte-by-byte volatile DMA->caller copy in UfsBioRead. V9 uses aligned scalarUINT64 reads/stores, fallbackCopyMem for unalignedcaller, byte fallback only for unalignedsource/tail. No SIMD/unalignedDMA loads; boundaries preserved. Hosttest allsource/destination offsets0..15,0..64KiBlengths,canariespassed. Benchmark now copies into poolbuffer and CRC-checks copieddata, reports read andcopy times separately. Actualfullpath speed pendinghardware. V8 read-only/HSnegotiation/70secpreEBS/10secENDpolicies unchanged. BuildingV9; phone stockAndroid.

V9 build2:27 success, packaged SHA256 7b8902ea7808c6d13666b415d8dfd6b010fd446e34247d279d625a731bc2d686 (artifacts/degas-ufs-copy-v9/boot.img). Verifieddegas/slotb/hash, flashboot_b/rebootOKAY. Await WBM test; read+copybenchmark now mandatory CRCchecks copieddata. No v9hardware speed claim yet.

## V9 no automatic reset, manual recovery; V10 diagnostic EBS watchdog
User waited>5minutes on standard startingWBM text, noautomaticreset, then manualreset tofastboot. Stockboot_b verified/restored. Capture evidence/raw/2026-10-25-degas-windows-v9-result hasempty pstore; debugloggerSHA c454db835334d197db209b1f6ef9af38e1f5a62b3fdbd75eea27dab7a8e13073 is identical V8previouslog. V9 speed/CRC/fullread/EBS NOT established fromthiscapture. Do not infer EBS solelyfromlackofreset.
V10 changes ONLY handoff recovery: Checkpoint defines DEGAS_DIAGNOSTIC_EBS_WATCHDOG1; successfulEBScallback stopsUFSasbefore but programs10sec reset-onlyWDT instead ofdisabling. Logs DEGAS DIAGNOSTIC EBS RESET ARMED seconds=10. NoBootServices/allocation/disk writes inEBS. Normalnonmacrobehavior retains WDTdisable forfutureproduction. Bothhosthandoffbranches PASS stoporder,registervalues,idempotency. User told explicitly thisdiagnostic resets evenifWindowsboots. Buildunderway,phoneAndroid.


## 2026-10-26 independent audits and V11
User initially prohibited flashing while away; three agents explicitly requested on gpt-6.1-sol/high. No device mutation until user returned and authorized UEFI testing. Audits and consolidated decision in docs/DEGAS-BOOT-DECISION-20261026.md. Two agents then examined GOP/panel and parent+third examined boot handoff as requested.
Confirmed actual shared VideoToVideo reverse-row overrun; regression fails old code/passes fix. Parent fixed QueryMode full struct copy, SetMode clear+refresh, single-CPU RAM ring IRQ serialization and missing successful large-read summary. Added bounded read-only framebuffer RGB/alpha samples, preflight memory/SMBIOS/time, EBS before/fail/event/core-final milestones. Core breadcrumbs gated DEGAS_BOOT_OBSERVATION, no altered EBS ordering. V10 watchdog retention kept (intentional 10sec post-EBS diagnostic reset). Sparse unchanged samples and event markers cannot establish kernel entry. Persistent UFS journal requested but NOT implemented/enabled; plan and prerequisites recorded.
Host tests: actual BLT overlap/canaries/overflow PASS; current BlockIO batching/error/output PASS; frame sampling bounds/read-only/blindspot PASS; RAM serial pendingIRQ/wrap/state restoration PASS. Full final build successful 2:25. V10 packaged only, never flashed.
V11 artifacts/degas-boot-observation-v11/boot.img SHA256 ffde6722630a8d5e3b00f40aee209b7ac877cc1d031286227bb22bb66c93f633, 67108864 bytes; FD SHA256 9a13481cc959636880c74ec161e247607520a5da03e363487404ee7dbb2f0dc2. Source hashes recorded beside image. Stock Android boot_b hash matched fa01819342dd24164dea568f88f9715bc3132f738a68c49453907982946c1113 before reboot. Root/product degas/slotb verified; fastboot product/slotb confirmed; flashboot_b and reboot OKAY. Await user WINDOWS BOOT MANAGER result and automatic reset->fastboot. No claim current failure resolved.


## V11 hardware result: Windows updates RAM, successful EBS boundary
Fresh 33669-byte console-ramoops-0 SHA256 063508b26bfda1beaffa3f25b6cae27d64924affd80d5d30b8138bd1ee70d2b8 in evidence/raw/2026-10-26-degas-windows-v11-result, equal debuglogger copy. Stock boot_b restored and Android booted; capture complete. WIM-sized request 647626752 bytes finished successfully in14901273us (~41.45MiB/s including copy), all11551 reads completed before EBS. At17.25s sparseRGB changedF5EBD5C5->659C09D1 and alpha1536opaque->1518zero/18opaque, same atEBS20.526s. UserstillseesoldSTARTINGtext: framebuffer mutation is now measured, paneldelivery missing. EBS finalcallback/memoryprotection/core-ready-to-return allpresent; HCE/queues stopped. DiagnosticWDT10s armed, consistent ~30.5seconds total. This establishes lastfirmwareEBSboundary, notkernelentry. SMBIOSabsent/invalidRTCday0/1.08GiBconventional remainseparateknownplatformgaps. Next opaquehardwareblend+boundedoneframeflush toexposeWindowsimage; GCEautonomousrefresh separatepending.


## V12 opaque framebuffer + final frame diagnostic, flashed
V12 implements guarded source-derived OVL constant blending (CON plainBGRX0x2000 only, low9alphaFF, PITCHbit28). Unknownformat retainsGOP-onlylegacyfallback; failedpartialsetuprollsbackwithreadback. No pitch/geometry/addresschanges. NewprivateFlushprotocol cachedbeforeStartImage; EBSarms10srecovery, saves122x272RGB565thumbnail inretainedRAM, thenflushesoneWindowsframewithprovenDSItransfer. NoBootServices/alloc/pixelwritesinEBS. ThisisnotautonomouspostEBSrefresh. GCEprototypeunlinked.
ActualGOPhosttestPASS: masks, rollback, unsupportedformats, pixelsunchanged,gBSNULLflush,timeout. ActualCthumbnailandPythondecoderPASS: read-onlyframebuffer,padding,knowncolors,corrupt/truncated/staletailrejection;138517bytesfits256KiBringwith34KiBlog. Fullbuildsuccess2:17.
Imageartifacts/degas-display-frame-v12/boot.img SHA256 f7225e9bd0a4bd48bb8a0447298c47260e554c120235490f55f16d8e5f18273f,64MiB;FD32f7983ee2962dbe5396d114749f1b0ef69289cead970db4daaa7c3d5ff744fc. Buildlog/sourcehashmanifest saved. Verifiedstockbackuphash,ADBdegas/slotb,fastbootdegas/slotb,imagehash. Flashboot_bandrebootOKAY. CurrentphoneV12Mu; awaitinguserWBMresultandautomaticresettofastboot. CollectfreshRAMafterstockrestore; decode tools/decode_degas_frame_thumbnail.py LOG OUTPUT.ppm.

## V12 hardware PASS: visible Windows logo and retained image

User saw the Windows logo, followed by the intended ten-second reset. Verified
fastboot degas/slot b, restored stock boot_b and booted Android. Complete fresh
capture at `evidence/raw/2026-10-26-degas-windows-v12-result`: console and
debuglogger both 172,554 bytes, SHA256
`f5575c471fd4d6aa126dd9790a3dbc2a5e8058488301cdaf522ffeedb672d984`.
CON values 0x21ff on both halves matched the guarded BGRX format; PITCH changed
from 0x1380 to 0x10001380 and read back correctly. Final flush Success and final
EBS core marker both present. Thumbnail FNV 45D248A2 decoded successfully into
boot-frame.ppm/png; it contains the Windows mark. This verifies explicit frame
delivery, not autonomous post-EBS scanout or kernel entry.

## V13 GCE read-only snapshot: built and flashed, result pending

Next missing data is inherited GCE clock/thread/translation state. Added four
Device MMIO mappings: SPM 0x1c001000/0x1000, MMINFRA clock 0x1e800000/0x1000,
IOMMU 0x1e810000/0x1000, GCE-D 0x1e980000/0x4000. No commands or clock writes.

Manual `GCE SNAPSHOT` replaces the already-proven console test. It arms the
ten-second watchdog before any new-domain read and logs a RAM breadcrumb before
each address. Staged guards: SPM MM_INFRA_PWR_CON at 0x1c001eac must have both
ACK bits 31:30 set; GCE-D/SMI/26M clocks must be ungated; GCTL must match exact
LK 0x20002. Then read globals, all 32 thread ENABLE words, thread0 detail only
if all inactive, and four IOMMU status/control words. No inferred page-table
dereference, token selector, FIFO, reset, SMC, permission or IOMMU writes.
Current Oct25 FDT GCE/IOMMU/SMI nodes equal the saved Sep14 nodes; all share
power-domain phandle0x41/id7, and IOMMU bclk is MMINFRA clock id2.

Actual snapshot host test passed strict GCC + UBSan: exactly 58 allowed reads
when ready, power/clock/GCTL rejection, active-thread detail omission, and
breadcrumb-before-read ordering. Full build successful in 2:12. The GCE
command prototype remains unlinked; candidates corrected to normal threads0..6,
excluding secure8..12. Command-fetch mapping/permissions still unproven.

Image `artifacts/degas-gce-snapshot-v13/boot.img`, 64 MiB, SHA256
`95780a0da95530a8579cffd847966dcd37b49b46419c6404c076cb2129c6b2d6`.
FD SHA256 `7da3c3bf3f56380a4892254e7c8ba3be385cf071d88a60272faed9cf6095a75e`.
Build log/source hashes saved alongside. Verified degas/slot b and image hash;
flash boot_b and reboot both OKAY. Current phone has V13 Mu. Await user selection
of GCE SNAPSHOT, result and automatic reset to fastboot. Restore stock boot_b,
boot Android and capture a new unique RAM log before further experiments.

## V13 hardware result and V14 mapping query

V13 completed. Stock boot_b was restored and the phone returned to Android.
Fresh capture: `evidence/raw/2026-10-26-degas-gce-v13-result/console-ramoops-0`,
11,034 bytes, SHA256
`19497b77c4ab738e40325ffa880630cdbca382359795921cce1426581c623270`.
The pstore and debuglogger copies agree. MM_INFRA power acknowledgements and
GCE-D/SMI/26 MHz clocks passed, GCTL is 0x20002, all 32 channels are disabled.
No new power or clock enable sequence is needed for this inherited state.

The IOMMU root readback 0xc0000007 decodes outside recorded DRAM under the
MediaTek table format. It must not be dereferenced. Exact saved GZ firmware
also intercepts Linux table-root writes; software and hardware roots differ.
Exact ATF names the setter of GCE global +0x54 bit17 `cmdq_set_va_mode`, but
V13's zero readback does not establish physical command-fetch bypass.
See DEGAS-GCE-FETCH-CONTROL-20261026.md and DEGAS-PANEL-AUDIT-20261026.md.

V14 adds the source- and exact-binary-backed query 0xbb00ffa4 with zero inputs,
the same query used before Android's IOMMU initialization. It runs only from
the manual GCE MAPPING CHECK, with the existing ten-second watchdog, the V13
power/clock/inactivity guards and CurrentEL=EL1. RAM breadcrumbs bracket the
SMC. Only a reply of 0 or 1 is classified as a valid boolean query result;
neither value grants a DMA mapping or enables the command engine. Four already
accessible registers are reread afterward. No table, GCE channel, protection,
clock or power writes and no table dereference are added.

Actual implementation host test passed GCC -Wall/-Wextra/-Werror and UBSan:
query ID and every zero input, before/after barriers, power/clock/thread/EL
guards, boolean replies, unsupported and unexpected replies. Build underway;
V14 has not yet been flashed. Phone remains stock Android at this point.

V14 build subsequently succeeded in 2:20. Packaged image
`artifacts/degas-gce-mapping-v14/boot.img`, 64 MiB, SHA256
`d5127759385765511c701e911f52822a5abc91b93c2ffb2bef5be3f9958b3ba9`;
FD SHA256 `8749641ef6f977276dd1f5a0d823778e643df2cd2b4df68a725ccfd4bcc4ff9e`.
Build log and source hashes saved alongside. Stock recovery image hash verified;
ADB and fastboot both confirmed degas / slot b. Flash boot_b and reboot OKAY.
Await manual GCE MAPPING CHECK followed by the ten-second watchdog reset to
fastboot. No command-fetch or continuous-refresh success is claimed.

### V14 hardware result and V15 mapping-only preparation

V14 completed and stock boot_b was restored. Complete capture:
`evidence/raw/2026-10-26-degas-gce-v14-result-complete/console-ramoops-0`,
11629 bytes, SHA256
`2e2b489d94ade6c655e43eb75416f7a7154959ec170c7a717afa2ae28104d2a9`.
CurrentEL=4 (EL1), SMC 0xbb00ffa4 returned a0=1, a1..a3=0. GCE/IOMMU
observations remained unchanged; all channels disabled. This establishes the
reported type2 availability, not a working DMA mapping or command engine.
The earlier incomplete capture directory contains ADB reconnect failures and
must not be interpreted as a failing hardware query.

V15 reserves 0x44440000..0x44470000 for owned NC tables/packet, below UFS DMA.
It queries all 256 pages of IOVA 0x10000000..0x100fffff before any root write,
aborting on errors or any NS/protected mapping. GZ synchronizes the whole
1 MiB leaf even for a one-page request. It records protected reservation and
framebuffer markers, registers the guest root, synchronizes one granule, and
requires exactly one NS 4 KiB page at PA 0x44451000, no other mappings and
unchanged markers. No GCE execution, security-mode changes or OS chainload.
All buffers remain quarantined until watchdog reset. A conflicting guest-root
registration can panic inside GZ; recovery relies on the armed watchdog.

Exact GZ evidence confirms A6 can query eagerly allocated hardware/protected
tables before normal guest-root registration; it does not require a fake guest
root. Host tests of the actual mapping control flow exercise all 518 query
failure positions, occupied granule rejection, incorrect PA/level/protected or
neighbor mapping, marker changes and exact SMC arguments. GCC strict warnings
and UBSan passed. These are software checks, not hardware success.

V15 review accepted the mapping-only scope. Before final build, explicit
DSB SY was added before guest-root publication and around synchronization;
progress breadcrumbs every 64 pages distinguish a stalled query phase.
Actual-control-flow tests passed again, including barrier ordering at the
root write and A7 call. Firmware sources are frozen for the final build.

V15 final build completed 2026-10-27 00:04:32 (2:15). Packaged artifact:
`artifacts/degas-gce-map-page-v15/boot.img`, 64 MiB, SHA256
`96279bfead8428fb3d3d8facfecf0c45841ce82c43493567ace0a51871acffe6`;
FD SHA256 `c41543821668493a8ce930e8cc795be8b5eede178302550ea1e217545dbb4f39`.
Source hashes, test results and build log are saved alongside. Verified ADB
identity degas/_b and fastboot degas/b, stock hash and current source hashes.
Flash boot_b completed OKAY, reboot OKAY. Await manual GCE MAPPING CHECK
and watchdog reset to fastboot. No hardware mapping success yet claimed.

### V15 hardware PASS (2026-10-27)

User saw GCE DMA PAGE VERIFIED. Stock boot_b restored and Android booted.
Complete capture `evidence/raw/2026-10-27-degas-gce-v15-result/console-ramoops-0`
is 12140 bytes, SHA256
`d7039105a2de2dd89f790953d4ce67f5da1742598eeea10e6e4c62ae88b76e45`;
pstore and debuglogger copies match. All 256 preflight/postflight checks passed.
Guest root write returned HW root 0x7bf00000, A7 returned 0, and A6 gave
`0000000044451a08` at IOVA10000000, matching PA44451000, TYPE2/LVL2.
All three marker queries were zero before and after (not evidence of a live
protected mapping). No GCE command was executed. Next is a bounded internal
SPR sentinel, with address-routing ambiguity resolved before enable.

### V16 internal command sentinel preparation

To avoid executing IOVA0x10000000 under an unproven physical-fetch hypothesis,
V16 uses identity IOVA=PA0x44451000. It preflights every NS/protected page of
0x44400000..0x444fffff, then maps only leaf index0x51 under root index0x444.
The same mapping-only checks run before submission; errors stop without enable.
No VA-mode SMC3, DEVAPC SMC7, clock, power or GCE global-reset change.

The three golden words are a080000012345678, 4000000000000000 and
1000000000000001. Packet only assigns thread-local SPR0 and finishes; no
display/memory write instruction. Encoded PC1088a200, END1088a202. Requires
all channels inactive, IRQs disabled/clear, source-backed thread0 reset and
programmed-CSR readback. Success requires SPR0 changed to12345678, counter
progress and PC==END without IRQ error bits0x12, within200ms polling budget.
Evidence is logged before suspend/reset/disable. Already-disabled tasks count
as suspended. IRQ acknowledgment uses W0C. Buffer remains reserved until
watchdog reset, also on cleanup failure. Success will establish command
fetch/execution, not which translation route was used or continuous refresh.

Actual source tests passed strict GCC warnings and UBSan for identity map,
full-granule failure paths, sentinel words, CSR allowlist, timeout/IRQ failure,
reset/readback/suspend failures and already-disabled cleanup. Build underway.

V16 review found no blocker; added final IRQ_STATUS/IRQ_ENABLE zero checks
so a failed W0C acknowledgment cannot report a fully clean result. Actual
source fault-injection test passed for ignored acknowledgment. Rebuilding.

V16 final build passed 2026-10-27 00:17:54 (2:28). Packaged64MiB image
`artifacts/degas-gce-command-v16/boot.img` SHA256
`7fb12f4c6b1545707c0fba20a8450c31a810cb04d30b94e01496578f4d2f318a`,
FD SHA256 `1d94ccb2b7d9a97f0b6dd35715438ad9485b2fe0addb292fa9323cd81ea1b806`.
Sources/image/stock hashes and degas slot b verified. Flash boot_b and reboot
OKAY. Await manual GCE COMMAND TEST followed by watchdog reset to fastboot.
No hardware command-execution success claimed yet.

### V16 hardware command execution PASS

Complete log `evidence/raw/2026-10-27-degas-gce-v16-result/console-ramoops-0`,
17211 bytes, SHA256
`adc1bb37347000eb26d3489d15c708b0c075f0b86d8750b874ec778ea0438101`.
Pstore/debuglogger match. Identity mapping verified. PC=end1088a202,
SPR12345678, execution count0->1, IRQ0. Cleanup suspended1/reset1/enable0/IRQ0.
No VA-mode change; mode register remained0. This proves the three-word
command fetch/execution, not which translation route was used. Stock restored.

### V17 one-frame diagnostic

54-word packet: proven CPU-equivalent18 display writes, three BUSY==0 polls,
SPR76543210, EOC and pass jump. Uses same bounded runner, verified identity
DMA page, readback/SPR/count/PC/IRQ checks and mandatory watchdog reset.
Full13-register OVL geometry/address/opaque/mutex/DSI-mode guard plus idle
before raw paint AND immediately before GCE enable. The pending frame adds
a white500x120 rectangle at x360/y1900, using stride4992 and fixed FBfcc2f000.
Cache clean and DSB precede submission. No GOP/Label/CPU display transfer
after paint on either success or failure; BDS returns only to RAM logging
and watchdog wait. User visibility is needed to prove actual transfer; packet
completion or a stale DSI status bit alone is insufficient.

Actual production packet checked by independent opcode decoder; fixed
pointers/geometry mismatch guards, full framebuffer bounds/padding/canaries,
cache ordering and runner failure propagation tested with strict GCC/UBSan.
Review recommended final pre-enable guard and fixed pointer checks; both
implemented/tested before build. General UFS writes and Windows boot unchanged.

V17 build passed at2026-10-27 00:28:59 (2:22). Artifact
`artifacts/degas-gce-frame-v17/boot.img` 64MiB SHA256
`0c33512a18169f665ed685a557658567b3e5e742d36716bfcd5864f531e35453`;
FD SHA256 `a855e7096daae1a917186477e4406c60a48845f124d02ec99566c1d009835af6`.
Final optimized guard test passed after fixing host mmap pointer provenance;
production unchanged during build. Image/source/stock hashes and degas/b
identity verified. Flash boot_b/reboot OKAY. Await user GCE FRAME TEST, white
rectangle observation and watchdog reset into fastboot. Require clean RAM
trace without exception-triggered ConOut refresh before declaring optical PASS.

### V17 optical transfer observed, completion race FOUND

User saw white rectangle. Complete log
`evidence/raw/2026-10-27-degas-gce-v17-result/console-ramoops-0`,17340 bytes,
SHA256 `76d701fd7bfc2a3a65fe52dae66d8bc9dbb69f853718cdcb32c65375fb4e1cf2`,
pstore/debuglogger match, no exception/assertion evidence. Packet completion
PC=end1088a235, SPR76543210, count0->1, IRQ0, cleanup fully passed. BUT final
DSI=80000016 (BUSY) and frame result52. The final idle-only poll passed BEFORE
DSI asserted BUSY. Optical transfer occurred afterward. Do not run the plain
continuous loop: packet completion currently does not serialize frame completion.
Stock boot_b was restored and Android capture completed.

### V18 observed BUSY handshake (single frame only)

Insert BUSY31==1 poll immediately after START=1, then previous BUSY31==0 poll.
No new register writes. Defined status register1400d00c/mask80000000 is
confirmed by exact installed DRM; the added assertion handshake is a diagnostic
inference from V17, not an existing copied Android flow. Missing a short pulse
can cause timeout even when image transfers; do not infer physical failure
from timeout alone. Packet57words, BUSY1 poll at index50, BUSY0 at53, sentinel54,
EOC55, endjump56; encoded one-shot END1088a238. Raw rectangle becomes green
(ff00ff00), no GOP after painting, same bounded runner/cleanup/WDT quarantine.

Host tests independently decode all57words and simulate delayed BUSY rise,
then fall; no EOC until both transitions. Never-rises and never-clears cases
remain blocked and hit modeled timeout without EOC. Guard/bounds/canaries and
runner fault tests passed strict optimized GCC+UBSan. Build underway.

V18 build passed 2026-10-27 00:38:07 (2:19). Artifact
`artifacts/degas-gce-frame-handshake-v18/boot.img`64MiB SHA256
`6e609d864a14ccd74ea80312472db05b83a566af2f2ed08b82435b2ff29f5c46`;
FD SHA256 `263c1ec052c900c0194d17850bdfafdddc9b542eb456358025b867473da7eac8`.
Source/image/stock hashes and degas/b identity verified. Flash boot_b/reboot
OKAY. Await manual GCE FRAME TEST, green rectangle observation and automatic
watchdog reset to fastboot. Do not assume handshake success from optics alone.

### V18 visible green frame, BUSY assertion handshake TIMEOUT

Capture `evidence/raw/2026-10-27-degas-gce-v18-result/console-ramoops-0`,
17372 bytes, SHA256 `aaf729243fa9e7c339320f362a109bd91db3499b4f618cfa565a0699d2275051`.
Pstore/debuglogger identical, no exception/assertion. User saw green rectangle,
but result46, PC1088a232 at BUSY1 poll, counter0, IRQ0 after200000 polling
iterations. Cleanup succeeded; finalDSI16. This does not prove missed pulse
versus GCE nonzero-POLL/read routing; do not declare the handshake successful.
Stock boot_b restored and Android captured. No loop is implemented.

### V19 fresh latched completion probe

Exact installed mtk_dsi_irq_status uses writel(~status,INTSTA) with bit4 in
mask4ffde. New packet full-writes INTSTA=ffffffef to clear onlyFRAME_DONE,
verifies bit4=0 beforeSTART, then waits bit4=1 and BUSY=0 before sentinel/EOC.
No CPU IRQ enable change, no RMW of W0C. Command-mode fresh bit4 remains an
experiment: Linux normally uses stream/CMD EOF events; old16 could be stale.
62words,19writes,5polls. Clear write42..43, zero poll46, freshdone poll55,
idle poll58, sentinel59/EOC60/endjump61; encodedEND1088a23d. Cyan rectangle.
Failure snapshots DSI and four GPR words before cleanup help distinguish
POLL/routing failure from missingcompletion. Same bounded recovery, no GOP
afterpainting, no Windows/loop changes.

Tests execute independent62word decoder with temporal DSI and initialstale16.
Delayedcompletion and veryshortBUSY pass; ignoredW0C, missingfreshflag and
stuckBUSY neverreachEOC. Exact19addresses/values/masks, bounds/canaries,
frameguards and runnerfailurepaths passed optimizedstrictGCC+UBSan.

V19 packaged and flashed to verified degas slot b. Firmware build completed successfully (2m27s); source manifest matched before packaging/flash. boot.img SHA256 42aec5ad57df6d7b4bbd7746b12462df9428243f50a19fe8857cf30084204ff4; FD a48a4ee84b428ca61fe4477d4521f0a2468200154f7f35d29cc7e90501a0f8d6. Stock backup hash verified; stock AVB tail retained. Fastboot send/write/reboot OK. Awaiting manual GCE FRAME TEST, cyan rectangle, automatic watchdog reset and fastboot return. This is bounded fresh FRAME_DONE validation, not an autonomous display loop or Windows boot test.

### V19 fresh flag visible to CPU, GCE POLL blocked
User saw cyan rectangle; automatic watchdog returned fastboot. Stock boot_b restored and complete Android capture saved in evidence/raw/2026-10-27-degas-gce-v19-result. Both console copies 17490 bytes, SHA256 b9726d30dd59c0de250b2ac46dd3923739a9690423607bf1aa15e867fd8236ff; no exception/assertion. Result46 at PC1088a237=index55 (fresh FRAME_DONE set POLL); CPU INTSTA16, R0=ffffffef, R1=0, R2=0,R3=0; counter0, IRQ0, cleanup successful. CPU sees flag set but GCE did not finish POLL. This rules out absence of that flag at the time of failure snapshot; it does not prove all prior zero POLLs actually read DSI correctly.

### V20 address GPR substitution, single-frame diagnostic
Pinned MediaTek source 3c4ccd07d5624879555ec710c11bb8818e5cb5c6 include/linux/soc/mediatek/mtk-cmdq-ext.h:122-129 warns R1/P0 overlaps mask R0. cmdq-test.c:31 selects R14 DEBUG_TIMER; :394-395 uses it in cmdq_pkt_poll with nonzero patterns. Exact installed encoder matches our previous bit layout, so leave mask/value unchanged. Substitute only address-GPR operands R1->R14: MOVE028e00001400d00c and POLL088e0001xxxxxxxx. Packet remains62words, same cyan rectangle, W0C clear/fresh flag/idle sequence, bounded runner and reset-only exit. Capture R0/R1/R14/R15 before cleanup on both success/failure (mailbox-ext.c:87,1930-1931 maps base80+4*n). Hardware result required; do not label this confirmed cause yet. No autonomous loop or Windows behavior changed. Source hashes frozen for build.
V20 build passed (2m08s), source manifest verified; FD SHA256 91cc1775c6c2707cba82a0c2dc0a51d2068a0c1fcbbc62a8f319df39466ff53a, boot.img SHA256 99488a5d32ec5480a399d385bc21039d79e3ec635038ddeb0a1f301341a19927. Verified degas/slot b, fastboot flash boot_b and reboot OKAY. Await manual GCE FRAME TEST (same cyan patch), watchdog reset, fastboot return.

### V20 full fresh-frame handshake PASS
Capture evidence/raw/2026-10-27-degas-gce-v20-result, both console copies17468bytes SHA256 fce7c1978e02704f0201b4b870281938ff0cc98d0967e65bf8e91d8b191f4a03; completecapture, noexception/assertion. User sawcyan. result0 after3196 pollingiterations, PC=end1088a23d, SPR76543210, EOC0->1, IRQ0, R14=1400d00c, R0=7fffffff, DSI16. Cleanup suspended/reset1, enable/IRQ0. R1->R14 substitution alone in packet fixes blockingpositivePOLL seenV19; oldR1readback0versuscorrectR14address supports GPRalias explanation. No claim every global registeralias detail proven. Stockbootrestored.

### V21 bounded autonomous display loop
Retain proven62-wordfreshFRAME_DONEpacket, replace lastpassjump with absolute100000011088a200; ENDonepast1088a23e pervendorfinalize_loop/mailbox. NoIRQEOC incrementscountereachframe. CPU performs20000monitorsamples with100us delay(~2secondsrequested plusMMIO/cache/logoverhead), requiresPCinsidepacket, enable1,noIRQerror, changingEOCcount at5000/10000/15000andfinalwindow. SPR0notcheckedasstable because nextframeusesitasaddress. CPUonlypaintsrawrectanglecyanthenred/green/blueandcleanscache; immutableGCEpackettransfersframes. Endingsuspend/reset/disable,W0C, thenread-onlyDSIidlewait<=100ms; bufferquarantineduntilwatchdogreset. NoGOP/CPUtransferafterpainting, noWindowsorEBSchange. MenuGCE LOOP TEST. ThreeoptimizedstrictUBSanhosttestsPASS; buildinprogress.
Initial V21 build rejected unused old GceFrameTest under -Werror. Extracted unchanged shared13-register guard into include-guarded GceFrameGuard.h and added packet include guard; Checkpoint includes only loop header. Host frame/loop guard tests rerun PASS; production sources refrozen, rebuild underway. No hardware flash occurred on failed build.
V21 rebuilt PASS (2m24s). Source manifest matches. FD SHA256 488f47dcb3004eea76c8891b551da087bf323ead29ceb4c3c7324aae9a34e9aa; boot.img SHA256 ff5efba76c2d0eff243c86383cd477601195685801036f5df2f5cbb07ac4f55c. Verified degas slot b, flash boot_b and reboot OKAY. Await GCE LOOP TEST, observed cyan/red/green/blue progression, auto watchdog reset and fastboot. No Windows test yet.

### V21 autonomous loop PASS, user saw all four colors
Complete capture evidence/raw/2026-10-27-degas-gce-v21-result, identicalconsolecopies17833bytes SHA256 1c67a7642b88086d515e62abe64b23f2d063247a6f5bdc83a5c327b35d9d4318; noexception/assertion. EOCcount44/88/cd atthreecolorupdates, final111hex=273frames, result0 IRQ0, PC1088a237(end1088a23e), cleanupcompleteandDSIdrain1. Autonomous repeatedframeupdates confirmed duringMu execution; notyetafterEBS. Stockrestored.

### V22 first Windows EBS autonomous refresh
Prepare mapping and immutable62wordloop inreservedNCpage beforeStartImage, leaveGCEdisabled. At successfulEBS: existingUFSstop+10sdiagnosticWDT, readonlythumbnail, finalCPUFlush nowfreshFRAME_DONEclear/readback/newflag/idle, irreversibleprivateprotocolCPUlock, hardwareguard+boundedGCEstart. StartupusesnoBS/allocation/SMCmap; waits3EOCs within2000*100us polls thenleavesloopactiveandreturns. NoCPUcallbackorBootServicescodefetchbyGCE. Errorstartupusestestedcleanup. Existingone-shot/testloopstillstopnormally. ReservedIOMMUtables/page remainquarantined. LateGOP/Flush/Query blockedbeforeBS/MMIO/pixelaccess afterlock. IfWindowsreturnsafterEBS, stoploopandCpuDeadLoop forWDT; neverresumeBSmenu. ExitBootServicesFailed callback stopsactiveGCE defensively (normalfailedMapKeyprecedesEBSnotifications).

Tests: optimizedstrictactualrunner coverspersistentstartsuccess/failure/invalidmodes; newWindowsstate testcoverspreparewithoutenable, packetcorruption, mappingfailure/repeatstart; actualGOP testwithgBSNULLcoversfreshflag,ignoredack,noflag,timeoutandlock. NoframebufferpixelwritesaddedatEBS;cachecleanonly. PostEBSWindowscacheattributesmaystillaffectvisibility, so staticpicturealoneisnotproofkernelhang. Buildinprogress, sourcesfrozen.
V22 build PASS (2m08s), source manifest matched. FD SHA256 d9863f621c2423801a3e25409ee74a71ca7acae3be9f1d009eb4498a6e505801; boot.img b37df41a4cf7ba3d425974b99f09772b5fcce10ac5e15b93ae2a0490d7fe30fa. Verified degas slot b, boot_b flash/reboot OKAY. Await manual WINDOWS BOOT MANAGER and observed screen, automatic10s-after-EBSreset (preEBS~70sbudget), fastboot return. Do not claim Windows kernel boot success until observed/logged.

### V22 Windows animated spinner observed
User saw Windows spinner moving, then halt/half-screen artifact before reset; returnedfastboot. Stockrestored, completecapture evidence/raw/2026-10-27-degas-windows-v22-result, identicalconsolecopies183726bytes SHA25632f778c455205e7bbd50ee550423bd6c3a1d02c62cef3b02cf72cfdbfc02631d. EBSsuccessful; GCEprep0/start0,220polls(first3EOCs),IRQ0, finalFlushLockSuccess, autonomous1; finalcoreEBSready-to-returnsuccess. NoUEFIexception/assertion. Ten-secondEBSWDTarmed; capturedoesnot independentlyproveexactresetcauseorWindowskernelhealthafterlastfirmwarelog. MovingWindowsoutputconfirmsliveposthandoffdisplay. Do notlabelartifactasconfirmedGPU/panelcrash.

### V23 user requested watchdog off
Set DEGAS_DIAGNOSTIC_EBS_WATCHDOG=0: existingOsHandoffelseclearsWDT_MODE enablebitwithkey22000000andpreservesotherbits; addexplicitRAMbreadcrumb. NoGCE/ACPI/memory/storagechanges. PreEBSrecoverybudgetandmenu/testwatchdogsremainunchanged; requestedWindowsrunhasno10sforcedEBSreset. IfunexpectedpostEBSreturn, CPUdeadlooprequiresmanualreset (commentcorrected). Buildinprogress,sourcesfrozen.
V23 build PASS (2m22s), sources verified. FD SHA256 1aa399978630ad4dc0e234d1cc9255f4fc1626bcc46f3771abc7f6227da409d4, boot.img 5351e16bef481c830ea9382dadba40549a71ec4c86313511a322573b375cf657. Verified degas/b; boot_b flash and reboot OKAY. Await user WindowsBootManager observation; EBS watchdog now disabled. Do not expect auto-reset after Windows spinner.

## Confirmed milestone: WinPE installer visible, V23 (2026-10-27)
User reports Windows installer UI currently displayed on Xiaomi14T/degas. V23 is the known booting baseline: boot.img SHA256 5351e16bef481c830ea9382dadba40549a71ec4c86313511a322573b375cf657, source/build/package records retained in artifacts/degas-windows-no-wdt-v23. Separate milestone record WINPE-BOOT-CONFIRMED.md created. No device operations on this report; phone left at installer. This establishes WinPE UI boot, not input/storage-driver/full-installation support. Next work must preserve this baseline and distinguish UEFI UFS boot reads from Windows UFS support.
