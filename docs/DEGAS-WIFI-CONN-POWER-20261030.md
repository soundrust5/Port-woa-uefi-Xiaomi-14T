# First CONN domain UEFI test — 30 October 2026

First hardware result: stopped at Code 2 / Stage 2 before any CONN writes. This tests the SoC digital CONN domain and its IP/host
CSR access, not an initialized RF die or working WLAN firmware transport.

## Evidence

Current FDT /consys@18000000 points power-domains to phandle0x41,index1.
Provider /power-controller@1c001000 is mediatek,mt6897-scpsys. Its ifr-bus
phandle0x71 resolves to1002c000/1000, NOT10001000. The latter is conninfra's
on-domain mirror/status window. Public MT6897 SCPSYS source revision:3c4ccd07d5624879555ec710c11bb8818e5cb5c6.
Captured mtk-scpsys-mt6897.c has CONN ctlE04, no basic/subsystem clock names,
no SRAM fields; DT has no conn-supply. Active Linux power callback selects
scpsys_power_on through IS_PWR_CON_ON and BYPASS_INIT_ON flags. All four bus
protection masks and table order are preserved. Source copied and hashed in
capture folder. Exact installed conninfra generated helper corroborates E04,
ACK30/31, low control bits and1002c000 provider, but is NOT the active Linux
callback. The active source's table reverse-clear order controls this probe.

Firmware gap resolved: current slot_b connsys_wifi_b is an8MiB partition,
SHA2564d77a63c2e4e1ddbd9da0a0127b83bb7850f2ea4d23517a81ab6832f9f071efe.
MTK header58881688/58891689, header512bytes, payload1614552bytes. Payload
contains SOC7_0 E1 /6637 strings. Firmware is absent from vendor files and
CONFIG_EXTRA_FIRMWARE is empty. Container copy and metadata are local evidence;
internal image loading/authentication and EMI placement still need decoding.
Do not publish this device firmware capture with source packages.

## Probe

Development macro DEGAS_WIFI_INIT_TEST defaults0. No automatic boot probe.
With macro1, simultaneous Volume Up+Down in UEFI activates it once. Normal
Mi image/menu retains two entries; no persistent diagnostic menu clutter.
The probe maps only four additional4KiB MMIO pages:10001000,1002c000,
18011000,18060000. SPM1c001000 is already mapped. Current EL1 identity
permissions checked before each I/O; this does not prove EL2/bus accessibility.
A10s reset watchdog covers stuck hardware transactions; finite polls use10us
steps up to1s. MMIO markers and raw snapshots precede the conditional reads.

Accept cold state only if PWR_CON[31:30,4:0]==0x12, all four owned protection
bits are set in enable+status, and SPM config bit0 already permits control.
No global SPM unlock writes. Set PWR_ON, waitACK30, wait50us, setPWR_ON2,
waitACK31, clearCLK_DIS, clearISO, setRST_B. Clear protection in selected
Linux reverse order:conn bit0,infra bit26,conn bit1,infra bit25. Check actual
protection and exact mirror checks. Only then read CONNSYS IP18011000,
expect02050400; only on that match read host ownership18060010 (bit2).
No WFSYS184b0010, RF SPI, PMIC writes, SMC, remaps, DMA, interrupts or firmware.
An already-on, fully unprotected domain is observed without writes; partial or
unexpected state is rejected without speculative recovery.

Owned cold transition is always followed by cleanup: set bus protection in
forward table order with acknowledgement, then ISO, clearRST_B, CLK_DIS,
clearPWR_ON/ACK30, clearPWR_ON2/ACK31. Verify original owned low control and
protection-enable bits. Failure to restore stops menu execution and waits for
reset. Unrelated protection bits are never written. Known reserved WLAN/EMI
RAM, Windows drivers/BCD, CPU/DVFS/display behavior, GZ/ATF/LK are unchanged.

Host GCC/Clang ASan/UBSan tests cover cold/on observations, exact mask/order,
SPM lock, partial state, ACK timeouts, failed protection, version mismatch,
cleanup failure, restoration, and preservation of unrelated bits.

Hardware instruction: hold both volume keys briefly in menu; record result,
then select ANDROID in the same menu. If watchdog resets a stalled test, choose
ANDROID on the next menu. Do not repeat the test or boot Windows until logs
are captured. No promise of FW-ready/scan/network support from an IP match.

## Hardware result and read-only follow-up

The first hardware test recorded CTL=02000012, CFG=1,
1002c000=00000010, 1002c1c0=00000000,
1002c00c=01002098 and 1002c1cc=ffffffff. Owned=0, writes=0,
restored=1. The two 10001000-window mirror fields were not sampled before
this early rejection; their original zero report was an unfilled result field.

A read-only debugfs regmap capture in working Android independently gives
1002c000=68000310, 1002c00c=69002398, 1002c1c0=0,
1002c1cc=fffffffe (later fffffffc), while 1000100c/100011cc=0.
Thus an almost-all-ones RDY status is not evidence of inaccessible MMIO.
The exact source uses BUS_PROT_IGN: clear_bus_protection returns without
waiting for clear acknowledgement. The original probe's requirement that
all RDY bits clear is stronger than the Linux implementation and unsupported
by the Android observation. Do not reuse that active power path as qualified.
The off-domain initial EN masks also differ from the original model and
still need reconciliation before an active transition and cleanup policy.

Android Wi-Fi was briefly disabled then restored using the normal service;
CONN remained on according to pm_genpd throughout, so this was not an Android
cold-power capture. No runtime-PM references were forced or removed.
Captured files: android-bus-protection-regmap.json and
android-wifi-toggle-protection.json under the same raw evidence directory.

Follow-up profile degas-wifi-aon-snapshot-v2 deliberately calls only
DegasConnAonSnapshot, reads the same six AON registers plus the missing two
10001000 status registers, and performs no connectivity-domain writes.
It does not read powered CONN ID or ownership registers. The ordinary
10-second diagnostic watchdog is fed during reads; Android/Windows routes
remain unchanged. Activation is the same simultaneous volume-key chord;
expected label is CONN AON SNAPSHOT SAVED. An ID of zero in this profile
is intentional because no chip-ID transaction is attempted.

## AON v2 result and active v3 contract

AON v2 passed on the phone. Both previously missing observations are zero:
1000100c=00000000 and 100011cc=00000000. The other six values reproduce
the first cold observation exactly. Code0/Stage2/ID0 means a complete AON
snapshot, not chip identification. Raw retained journal and summary are in
evidence/raw/2026-10-30-degas-wifi-aon-v2-result.

The v3 active probe accepts two OFF baselines with CTL[31:30,4:0]=12:
all four owned EN bits set, or all four clear (observed LK baseline).
Mixed owned EN states remain rejected. It follows the same Linux power-on
and protection-clear ordering, with EN readback checks instead of forbidden
RDY-clear polling. BUS_PROT_IGN is respected; both on-domain mirror checks
still gate the powered CONN version read. No RF, SMC, PMIC, firmware or DMA
operations were added.

Cleanup deliberately finishes OFF AND PROTECTED, using Linux forward
protection SET order, EN plus RDY-set acknowledgement, isolation/reset/clock
and both power-off acknowledgements. It preserves unrelated EN/control bits.
It does NOT recreate the loader's unprotected-off EN=0 state. Therefore
Restored=1 in v3 means OFF AND PROTECTED, not bit-identical LK restoration.
After every owned transition the UEFI wrapper arms a fresh 10-second
watchdog and stops menu execution until reset, including on successful ID.
After reset the user can select Android. If cleanup fails the same reset
path is mandatory; no direct Android/Windows jump can occur from that test.
Host GCC and Clang ASan/UBSan checks cover the observed baseline, sticky
RDY in cold/on states, both blocked mirror cases, failed EN clear, ACK
failures, cleanup failure, partial baseline rejection and unrelated bits.
Hardware success is pending. No conclusion about a usable WLAN link yet.

## V3 hardware result / pinmux follow-up

V3 reached both SPM ON acknowledgements (CTL c200000d) and EN-clear readback.
Both 10001000 mirror checks were zero. IP read 18011000 returned DEADFEED,
so the probe correctly did not read ownership. Code6/Stage130, writes18.
Cleanup completed: CTL02000012, EN06000010/3 (OFF AND PROTECTED).
This proves the AP-side power transaction, not working internal CONN access.
Android menu routing subsequently returned to UEFI according to the user.
Stock boot_b was restored from fastboot; rooted Android completed startup,
and the complete test journal survived. Cause of route failure remains open;
do not attribute it to damaged Android or lost journal.

Exact ELF follow-up decoded consys_hw_pwr_on: first cold start obtains clock
type, passes platform config/clock type through init ATF, requests PMIC common
power, calls if_pinmux, then powers the SCPSYS domain and polls chip ID.
Clock type is conn_hw_env+0xc; it is no longer an unidentified field.
Current Android log explicitly reports tcxo_gpio=0, tcxo_support=0 and
52M co-clock. This identifies the normal clock mode, not a validated UEFI
secure-init call or proof of the missing physical clock.

The exact installed if_pinmux(enable=1) writes:
- GPIO10005480 mask77777777, value11111111 (pins192..199)
- GPIO10005490 mask00000777, value00000111 (pins200..202)
- IOCFG11b20030 mask001ff000, value00049000
- IOCFG11b200d0 mask0000000a, value0.
The source pin description identifies mode1 as CONN_TOP_CLK, CONN_TOP_DATA,
CONN_HRST_B and remaining CONN signals. Current /proc/mtk_gpio confirms all
pins192..202 are mode1 in Android. Extracted sources and Android snapshot
are stored next to pinmux-init-followup.txt (exact ELF disassembly).

V4 is an isolated addition of this source-qualified pinmux before SCPSYS.
It snapshots all four words before any change, writes only owned masks,
verifies readback, and restores owned fields in reverse after successful
OFF AND PROTECTED cleanup. A failed power cleanup deliberately leaves pins
alone until mandatory reset. Each owned path resets; Windows is not a test
target. The expected CONN version is polled up to11 reads, with ten1ms
intervals, matching the installed non-ATF polling helper's retry count.
Every sample is logged. The active ATF chip-polling implementation is not
claimed equivalent; it remains a separate layer.
PMIC and ATF init are still omitted in this isolated test. Failure would
not rule out those required prerequisites. No GZ/ATF partition changes.
Host GCC and Clang ASan/UBSan cover pin RMW masks, full restoration, failure
at each pin write/readback, delayed ID ready on last sample and bounded
failure, in addition to the prior protection/power checks.
For V4, return to fastboot after the mandatory test reset; do not rely on
the currently intermittent Android route. Restore Android before collecting.

## V4 hardware PASS

Retained journal confirms Code0/Stage131, CONNSYS IP02050400 and host
ownership00000004 (bit2=firmware-owned). IP samples are five DEADFEED
followed by the matching ID on sample6, nominal5ms after first read.
This is a matching digital CONN register plus accessible host CSR, not
running WLAN firmware, A-die/RF readiness, WFSYS readiness or networking.
No ownership transition was requested. No PMIC or secure calls were made.
Pinmux and ID polling were both added; individual causality is not isolated.

Initial GPIO MODE values were 10005480=0 and 10005490=11111000;
IOCFG11b20030=8009, 11b200d0=3ff. The configured fields were verified,
then all four owned fields restored after successful protected power-off.
Total writes26, Owned1, Restored1, PinsOwned1, PinsRestored1.
CTL returned02000012; protection EN finished06000010/3 as designed.

Phone returned to fastboot; stock Android boot_b was restored, Android
completed startup and the full journal was captured successfully. The user
again reported failure of the menu Android route; it remains unresolved.
Current on-device boot_b is stock Android, not the experimental image.
Evidence: evidence/raw/2026-10-30-degas-wifi-pinmux-v4-result/summary.json
and console-ramoops-0; qualified private profile and hardware-result.json
under artifacts/degas-wifi-pinmux-v4. No public release updated.
