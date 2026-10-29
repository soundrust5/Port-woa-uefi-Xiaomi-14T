# Degas touch bring-up, 2026-10-27

## Confirmed on this phone

Read-only Android capture: `evidence/raw/2026-10-27-degas-touch/`.
Actual binding `/sys/bus/spi/drivers/focaltech_ts`, active module
`focaltech_touch_rothko.ko`, SHA256
`2dd865855c329eaee6c246b701cc6004dee0d692bc232aebdfa9f312d1d53b00`.
The initially noticed ft3518u name was from an attempted generic modprobe
list, NOT the active module. Goodix alternatives in DT are not proof of
the installed controller. IC identification is 0x3683, CID type 0x92;
commercial part name remains unconfirmed.

Driver sysfs reports SPI mode 0, max 15MHz, 10 contacts,
coordinate ranges 0..121999 and 0..271199. Android input event6, SPI2 CS0.
DT host compatible mediatek,mt6985-spi, register 0x11012000 size0x100,
GIC SPI244 (INTID276), tickdly3, pad-select0, mainpll source.
Touch IRQ is GPIO9, reset GPIO60, AVDD GPIO73; Linux global GPIO numbers
271/322/335 are NOT SoC-local indices. Android virtual IRQ56 must not be
copied into ACPI as a GIC interrupt. GPIO interrupt routing still needs work.

## Protocol core implemented, no hardware writes

`diagnostic/degas/touch/fts_protocol.h` independently implements bounded
SPI read framing and response validation. Evidence: disassembly of actual
phone module, fts_read 0x33a8c; header writes at0x33b74, status at0x33c34,
CRC at0x33c54..0x33d04. Request addr,A0,lengthBE16, three dummy bytes;
response payload starts at7; CRC16 reflected0x8408 seededFFFF, little endian,
no final xor. Total length payload+9. Status byte3 maskA0 must be zero.
Offline tests cover every payload size1..512, malformed/truncated/status/CRC
failure and unchanged destination on rejection, with ASan/UBSan.
No SPI MMIO implementation, touch-coordinate packet parser, ACPI device or
Windows hardware driver has yet been deployed. Do not claim working touch.

User gesture capture includes X/Y, slots0..2 and35 release events; summary.json
records observed ranges. Kernel input events are already parsed, not raw SPI
frames. Need recover report parser from module / compare raw frames before
implementing Windows reports. Reuse OP12 VHF architecture only, not its TCM
commands or Qualcomm SPI registers.

## UFS journal prerequisite

## Contact decoding follow-up

UEFI touch-pins-v1 replaces the GCE LOOP menu test with TOUCH PIN SNAPSHOT.
Read-only GPIO9/11/12/13/14/60/73 mode/direction/output/input, using MT6897
pinctrl mode/dir/di/do field tables from evidence/sources/degas-buttons.
No gated SPI MMIO reads, clock changes, resets or power writes. RAM DEBUG
output survives the normal ten-second watchdog reset; return directly to
fastboot after test. Android reference immediately before deployment:
android-gpio-before-uefi.txt (IRQ high, reset high, AVDD high).

Hardware result: evidence/raw/2026-10-27-degas-touch-pins-v1/console-ramoops-0
contains full BEGIN/END snapshot and clean menu exit. GPIO9 mode0 input high;
GPIO11..14 mode1; GPIO60 reset mode0 output LOW; GPIO73 AVDD mode0 output LOW.
Thus SPI mux is inherited but reset/AVDD enable are not in Android active state.
This is a missing initialization prerequisite, not evidence of a broken panel.
Stock boot_b restored and Android booted after capture.

Android regulator_summary saved in regulators.txt: mt6368_vtp supplies
spi2.0-iovdd at1800mV; DT phandle0x2a8 confirms same rail. UEFI VTP state is
not yet measured. Do not merely raise GPIO73/60 before checking this rail.
power-disassembly.txt records exact fts_power_source_ctrl/reset/init functions:
reset low, 2000..2100us, power_source_ctrl_simplify, 500..510us, pinctrl state.
Need finish resolving pinctrl and VTP control before first powered SPI test.

VTP source checked at MiCode MTK_kernel_device_modules commit
3c4ccd07d5624879555ec710c11bb8818e5cb5c6: mt6368-regulator.h specifies enable
1c87 bit0, low-power bit1, VOCAL1da4 low nibble, VOSEL1da5 low nibble.
ldo_volt_table1 selector4 is1800000uV. New TouchPower.h reads these three
registers via bounded PMIF SWINF2 SID5; no regulator write operation added.
PMIF host tests cover SID5 encoding, invalid SID/address, bounded timeout,
and unchanged output after failure; ASan/UBSan passed.

Full Windows prerequisite: current driver only permits WRITE10 within scratch
LBA29800448..29865983 and forces MODE SENSE write protection. Must add and
qualify a partition-bounded filesystem/boot mode before installation; never
simply enable whole-LUN writes. Existing clean install-pro.wim metadata:
Windows11Pro ARM64 ru-RU build26200.8037, image1, 6871669729 compressed bytes.
OP12 prepared image and driver package must NOT be reused on degas.

Report2 dispatcher now implemented: high nibble payload[1]==2 (0x861c,
0x8648), count low nibble1..10 (0x87a0), records offset4 stride8.
Preserves event1 releases, rejects duplicate IDs, unknown types and truncated
frames without modifying output. Live panel packet type remains unconfirmed.

mtk_spi_fifo.h adds an unbound callback-based single-packet FIFO engine for
1..32 bytes. No DMA, bounded polling, reset on timeout, unchanged RX on error.
Requires exclusive host ownership, clocks/pins/timing prepared externally.
It intentionally cannot split a long touch report across chip-select cycles.
ASan/UBSan model tests cover all lengths, tail bytes, timeout and DMA rejection.
Still no platform MMIO adapter or Windows driver deployment: clock/pin/CS
initialization and live transaction verification are outstanding.

Added fts_contact.h: isolated eight-byte contact decoder reconstructed from
fts_irq_handler 0x8ba8..0x8c58. X/Y are packed 16-bit fixed-point values;
Android applies (coordinate * super_resolution) >> 4, with board scale100.
This is one branch, NOT a confirmed dispatcher for the live packet format.
Synthetic exhaustive coordinate tests and invalid ID/event/length/range tests
pass under clang ASan/UBSan. Hardware packet replay is still required.

Inspected fts_touch_point_show before using its sysfs interface: it loops over
0x880 bytes from cached report, repeatedly supplies snprintf size0x1000 while
advancing its destination (0x12bd0..0x12bf4). Do not read this attribute until
its actual rendered length and allocation safety have been established.
An unprivileged read was denied; no privileged read was issued.


New logger uses two64KiB raw slots at LUN2 LBA29800448 and29800464, within the
qualified Windows scratch extent. Payload (inventory/setupapi/wpeinit tails)
is flushed first, then commit header with CRCs, then flush. Android exporter
`tools/read_degas_ufs_journal.py` accepts only CRC-valid slots and chooses by
session/time and sequence. This is not a filesystem file, nor a kernel crash
dump; errors before user-mode logger / working UFS are still RAM-only.
Stress no longer runs automatically in logger. Hardware journal test passed:
user observed saves increasing with error=0. After restoring stock boot_b and
rebooting Android, tools/read_degas_ufs_journal.py recovered both committed
slots (sequences 28 and 29), with valid header and payload CRCs. Evidence:
evidence/raw/degas-ufs-journal-20261027-134930/. GPT verification also passed.
Recovered inventory reports degasufs Started=1, Problem=0, NTSTATUS=0.

INF now requests StartType0, but host DISM offline injection failed Error87
in get_OSVersion before driver install, as previously on OP12. Candidate was
unmounted/discarded. Merely changing INF does NOT prove boot-time loading.
Current fallback loads via drvload before wpeinit (not after its completion).
True early boot installation remains outstanding; prefer native ARM64 DISM
servicing rather than inventing DriverDatabase registry entries.

Journal hardware test dispatched: boot-degas-inventory-20261027-103820-cca413409f04.wim
SHA256 fad695b420bf38b989182777ce5b55bbe609445402e3d0e72bfd282b0386f485,
ESP readback and GPT verification passed. Stable V25 UEFI flashed boot_b and
rebooted, user asked to select WBM and report journal saves/error. No watchdog.
Driver SHA8d26ad5843a8892e48694d1d70925c45541b622fc55093fd6a84e06710650155.
After user returns fastboot, restore verified stock boot_b, boot Android and
run tools/read_degas_ufs_journal.py. Do NOT rerun stress verifier expecting
all256MiB unchanged: the first128KiB is now intentionally journal storage.
Stage script currently accepts only original/stress pattern states, so next
deployment after logging requires a new checked journal preimage policy.

## Touch power snapshot and bounded enable test
Hardware snapshot evidence: evidence/raw/2026-10-27-degas-touch-power-v1/.
Retained console SHA256 2bb53240a09d1a81a725661ae34a9467f438227a55b8f48db198e4e12ddd7aaf.
SID5 reads: 1c87=00, 1da4=00, 1da5=04, all error0. VTP disabled,
selector already1.8V. GPIO60 reset andGPIO73 AVDD output low; IRQ9 high.
Stock boot_b was restored before capture. dmesg capture incomplete; RAM console valid.

power-v2 adds bounded VTP enable-bit RMW with full-byte readback, AVDD enable,
reset release, then reset/AVDD/VTP restoration. Voltage/calibration unchanged.
Power order uses IOVDD then AVDD; not claimed identical to every Android call.
PASS verifies software register sequence/cleanup, not chip identity or touch.
PMIF host model tests passed ASan/UBSan, including dropped write and bit preservation.
UEFI build passed. Image SHA256
06281854dda377cfd54691ced428406ee4a454cbac5dfdff164839d8b9dac47e.
Flashed boot_b on verified degas slot b; awaiting TOUCH POWER TEST hardware result.
Next: collect retained RAM after user returns fastboot, restore stock Android,
then bounded SPI chip ID query. No full Windows installation performed yet.

Power-v2 hardware PASS confirmed in retained console:
evidence/raw/2026-10-27-degas-touch-power-v2/console-ramoops-0
SHA256 e02b0451dad47c544df971b0c13c64bb9283e0bbd7d0740dd63d93a0f96bd947.
iovdd_enable=0, active error=0, cleanup=0 initial_en=0.
Restored stock boot_b and captured Android before further testing.

Next candidate TouchSpi.h queries A3/9F using CRC-checked 10-byte FIFO frames,
no DMA or sensor firmware writes. Explicit 26MHz parent0 and 1MHz SCK;
DT pad0, mode0, tick-delay3, IPM nonidle/software-CS. Clocks and host config
restored after query; power cleanup always follows. At most5 attempts/register,
20ms transfer timeout and200ms retry gap, expectedID3683. This is not a HID driver.
Added MMIO mappings for TOPCKGEN/PERICFG/SPI2. Offline FIFO and protocol tests
pass ASan/UBSan; hardware response remains unverified.
Identify-v1 UEFI build passed; boot.img SHA256
e37cd3b6a92ebcc1b20cba3d79278cf966864ac6ac110729ecebaf8f5391a8b1.
Flashed verified degas boot_b and rebooted for TOUCH IDENTIFY TEST.
Latest Android dmesg explicitly reports driver_version=FT3683:2024.07.10-001,
fw_version=3c, alongside chipID3683. Firmware in TP and host both3c; no upgrade
needed in Android. Hardware identify result pending.

Identify-v1 failed before transmitting: transfer=3, ID0000 error27.
Clock mux01010101/gate00000000 restored; power cleanup0.
Capture evidence/raw/2026-10-27-degas-touch-identify-v1-read/console-ramoops-0
SHA55a69feff452a00817f787b8fa4abd97b180d7cd357bd84b0b9381e9438a237c.
First capture attempted before ADB ready, empty failed files in identify-v1/;
second capture fully completed. Do not interpret this as absent endpoint reply.
Core error3 means incompatible CMD bits or nonzero preexisting STATUS1;
v1 omitted these register values. v2 adds prepared and rejection snapshots,
without relaxing the guard or changing power/timing. FIFO model now tests exact
IPM/software-CS/tick-delay CMD requested by UEFI and passes ASan/UBSan.
Identify-v2 build passed, flashed boot_b and rebooted. Image SHA256 e1b2ca9e2cee650e0c3f4201ddfed94802b4436c8d13eb98f6210c82e57a0c23. Awaiting diagnostic register snapshot from TOUCH IDENTIFY TEST.

Identify-v2 retained log: evidence/raw/2026-10-27-degas-touch-identify-v2/.
Console SHA256 bb85984e70a27e17159c425a245200d8e5602a4ad64b4a8ee426f509cf179d69.
Prepared CMD=00CC3000 (exact expected), STATUS1=1, STATUS0=0;
CFG0=000C000C CFG1=0000000C CFG2=000C000C PAD=0. transfer3 -> error27.
The pre-submit STATUS1==0 requirement in our FIFO core caused rejection.
Captured Android polling routine does not impose that prerequisite: it writes
ACT then polls STATUS1 and consumes STATUS0. v3 removes the unsupported
precondition and clears STATUS0 before submission; mode/DMA checks unchanged.
Host model now reproduces idle STATUS1=1, STATUS0=0 and active0 -> completed1.
All 1..32-byte FIFO cases and timeout/DMA checks pass ASan/UBSan.
Endpoint identity still unverified; no claim that transport is working yet.
Identify-v3 build passed and flashed to verified degas slot b. boot.img SHA256 a8766aa10eac02ca2d0580fa385b4678b0a67c437d5c21f507ca49154acc5f7b. Awaiting hardware result after removal of incorrect idle STATUS1 guard.

Identify-v3: all5 A3 transfers return0 but RX is10 zero bytes; CRC rejects,
error23. Power cleanup0. Complete capture in
 evidence/raw/2026-10-27-degas-touch-identify-v3/.
This does not prove endpoint communication. Source comparison found omission:
mt6985 sw_cs=true; mtk_spi_set_cs(false) sets CMD CS_POL bit7 before transfer,
set_cs(true) clears it and resets host. Our prepare_message-only translation
set CS_PIN_SEL but never asserted CS_POL. Also mtk_spi_transfer_one explicitly
writes CFG3=0 for full-duplex; we had inherited CFG3.
Identify-v4 adds CS assertion/deassertion/reset around each message and sets
CFG3=0, saving/restoring CFG3 with other config. Same power/clock/framing.
FIFO model tests with CS_POL asserted pass ASan/UBSan. Hardware pending.
Identify-v4 build passed, flashed degas boot_b and rebooted for test. SHA256 850e2972f176972efc4472509e3def53978b72727057f89c88953795f9caf5a8. User must select TOUCH IDENTIFY TEST then return fastboot after automatic reset. Endpoint ID remains unverified.

Identify-v4 SUCCESS confirmed, not just screen wording:
A3 RX0101010101010136325B; 9F RX010101010101018314B9.
Both CRCs pass first attempt, ID3683 error0; clock restore0 and power cleanup0.
Evidence evidence/raw/2026-10-27-degas-touch-identify-v4/.
User saw blank numeric ID because Label supported only A-Z; added digit glyphs.

Packet-v1 candidate reads16-byte partial report at address01 (from Android IRQ
handler) in bounded20-second polling window, 25-byte total FIFO frame+CRC.
No coordinates claimed yet, no full report parsing. Logs up to64 changed valid
payloads, good/bad/changes/IRQ-low counts. This validates framing and packet type
before extending transport beyond32-byte FIFO. Watchdog fed only during bounded
capture, cleanup and10-second reset preserved. User must tap/swipe in capture.
Packet-v1 built and flashed verified degas slot b; SHA256 f9bada35bdcecd9d8d46c0a2408fc1ffa012fe6332026825c7d5496e78906de0. Awaiting20-second user tap/swipe sample then automatic reset to fastboot. Digit rendering fixed, not yet visually confirmed.

Packet-v1 capture complete: good989 bad0 changes982 irq_low62.
Logged payloads A55F + incrementing BE frame counter, not assumed coordinates.
Evidence/raw/2026-10-27-degas-touch-packet-v1/. Transport CRC passes, not touch HID.
Disassembled actual module fts_enable_touch_raw: boolean enable maps to41/00,
writes9E then reads9E and compares, retries with1ms delay. Captured fts_write
builds8-byte single-register frame (addr,00,0001,dummy3,value), status maskA0.
Packet-v2 scope: read9E first, accept only00 or41; if41 write00/readback, capture
same16 bytes, restore41/readback. Abort on unknown or failed readback. Reset and
power cleanup still follows. No firmware flash; candidate hypothesis only.
Additional disassembly saved mode-disassembly.txt and set-mode-disassembly.txt.
Packet-v2 built and flashed verified degas boot_b, SHA256 b22a85c3647080b2972ada5fe0062bd8858656162fa006047e36308790697778. Awaiting user packet capture. Full Windows and Windows HID touch are not installed by this test.

Packet-v2 success:9E initially41, changed00/readback; good988 bad1 changes113,
irq_low105, restore41 verified. Evidence/raw/2026-10-27-degas-touch-packet-v2/.
After transient A55F/zero records, packets0021FFFF... contain one type2 contact.
Captured 0021FFFF82800367C40B1621FFFFFFFF decodes ID0 event2 X64075 Y87125
in Android100x coordinates. Actual fixture added to ASan/UBSan decoder tests;
release event and incomplete two-contact rejection verified.
Draw-v1 maps validated single contact to GOP1220x2712, draws green9px marks,
ignores release/unknown/CRC-failed records.20sec bounded window; restore9E and
power afterwards. Multiple contacts not yet supported by16-byte capture.
This is UEFI visual validation, not Windows HID. Full Windows still pending.
Draw-v1 built and flashed verified degas boot_b. SHA256 38bb75ead0f6e3ab012ba0b2137de239c0d07439c9a74e92e99eb5063c51a8f0. Awaiting user confirmation that green marks follow one finger with correct orientation and position.

DRAW-V1 VERIFIED BY USER: green trace follows finger correctly, including
orientation and tested edges. Stable artifact artifacts/degas-touch-draw-v1/boot.img
SHA25638bb75ead0f6e3ab012ba0b2137de239c0d07439c9a74e92e99eb5063c51a8f0.
Retained evidence/raw/2026-10-27-degas-touch-draw-v1/ fully captured after stock
boot_b restoration. ID3683, good885 bad0 changes264, contacts256 releases585
(repeated UP packets counted), rejected44 (initial/unknown packets ignored).
9E restored41/readback, power cleanup0. Android restored and available.

Next Windows work must preserve this working hardware baseline. Reuse VHF/HID
state-machine lessons from OP12, not its Synaptics protocol or Qualcomm GPIO.
Need MediaTek resource ownership/ACPI, serialized passive SPI, validated mode
switch/readback, release on stale data and teardown, driver package and matching
INF/VHF lower filter, WinPE binding and UFS-journal diagnostics. Current16-byte
transport only qualifies one contact; reject partial multiple-contact packets.
Full Windows requires separate UFS boot/write qualification; not done by draw test.

## Windows touch v1 candidate, 2026-10-27

User requested careful Windows port with agent review. Main implemented
`diagnostic/degas/touch/windows/degastouch.c`: exact verified power/SPI sequence,
five strictly checked ACPI resources, bounded FIFO at PASSIVE_LEVEL, no DMA or
IRQ programming, validated ID3683 and9E mode transition. First D0 only; resume
explicitly unsupported. One-contact16-byte capture; partial multi-contact
packets rejected. Source driver ACPI DGS0002/service DegasTouch.

Agents implemented/reviewed VHF descriptor/state machine, ACPI/INF/build/signing,
and WinPE panel. Local test signature (not Microsoft signature). VHF LowerFilters
and PE-only `HID_DEVICE_SYSTEM_VHF` fallback learned from OP12. Unchanged Microsoft
mshidkmdf.sys donor hash pinned. Portable ASan/UBSan HID tests cover release,
ID replacement, stale250ms, failed submission, bounds and descriptor layout.
ARM64 driver, ARM64/x64 panel, x64 selftest and both INF validation passed.
Hardware review found/fixed lost primary error during cleanup and missing poll
join on Surprise. No hardware success claimed before this test.

Panel upper half reports touch PnP/transport/HID and UFS journal status; bottom
black canvas draws only actual Windows pointer/mouse input, not raw sensor data.
TouchDiagnostic registry ABI1 is retained in existing two-slot UFS journal.
Journal predeployment validation now accepts two CRC-valid journal slots only
with exact qualified stress pattern across every remaining scratch block;
predeployment slots saved. GPT and WIM readback verified.

Candidate `artifacts/degas-touch-windows-v1/boot.img` SHA256
6ba2d9150c5fb199ffd679fb81b358fb128f1f03751117f61134fcd137172a03.
Signed driver SHA25611106365872efb73b0a6d3665b7495b87cfd63ad9c675fd90046ff32fa947447.
WIM SHA2564478e685f168162e0e5179dd7c127ad2a8ed8a6fbfd2821e93dd4f4367073642.
WIM staged/readbackpassed; boot_b flashed OKAY on verified degas slotb and rebooted.
Awaiting first Windows touch result; no automatic post-Windows reset enabled.
UEFI draw-v1 remains known-good control. UFS driver unchanged.

### First Windows hardware success and two-contact candidate

User confirmed actual Windows touch works including screen edges. Exported
`evidence/raw/degas-ufs-journal-20261027-160813`: seq25/26 CRC-valid,
touch devices1/VHF1/source bind0/child bind0, WindowsInput706. Preserved source
and signed package under `artifacts/degas/touch-windows/one-finger-pass-v1`.

Next candidate uses20-byte payload/29-byte SPI frame (still bounded32-byte FIFO)
for two complete contact records. HID parallel report14bytes, maximum2, stable
IDs, independent release, missing-ID cleanup, stale all-up and failed submission
recovery. ASan/UBSan tests cover pairs/reordering/replacement/faults. WinPE canvas
assigns two colors to live pointer slots, shows active and peak Windows pointers.
No new UEFI/clock/power sequence. More than2contacts remains unqualified/rejected.

User also authorized full Windows11Pro ru-RU local Xiaomi without password.
Installation preparation runs independently. IMPORTANT: current raw journal
LBA29800448 is inside the future NTFS Windows partition; export and disable it
in installer PE BEFORE formatting. Never run this raw-journal PE after NTFS is
populated. Full-Windows source and installation-scoped UFS candidate are separate
from this proven touch test; full OS not installed at this checkpoint.

Two-contact test deployed: touch SYS SHA256
f83eedc2c7f22bb9d193655a2e8ab0af44b68af72a22431446e126c62e1f8446.
Final logger canvas assigns colors by active pointer slot; active/peak counters.
Final WIM deployment readback/GPT passed (evidence/build/degas-touch-multi-stage-retry.log).
Same proven UEFI boot_b image flashed/rebooted; awaiting user two-finger test.
Earlier final-stage attempt timed out on read-only scratch capture while7GB
source upload occupiedADB; retry after upload completed passed. No partial ESP
write occurred in failed attempt.

Full Windows clean source now on Android `/data/local/tmp/degas-install/install-pro.wim`,
6871685563bytes SHA2564400c3d350b71fbbf09d9e700c8ff9b1264d92707400e8d8fbff7ce47e5d424e.
Metadata Pro ARM64 ruRU26200.8037; remotehashpassed. Noformat yet.
Installation UFS candidate in artifacts/degas/ufs-windows/install-candidate;
default proven signed package NOT replaced. Compile opt-in DEGAS_UFS_INSTALL_MODE=1
allows only captured Windows/ESP LUN2ranges; actual FLUSH/SHUTDOWN and FUAchain.
Hosttests/ARM64compilepass, PE/filesystem/earlybootqualification pending.
Tools/artifacts full Windows prepared by full_windows_prepare agent; inspect its
final report and scripts before deployment. User authorizes Pro/Xiaomi/no password.
