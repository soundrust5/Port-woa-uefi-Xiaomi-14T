# Android one-shot route observation — 30 October 2026

Current Android is restored and boots from the stock boot_b. The intermittent
UEFI menu route failure has not yet been diagnosed or fixed. The route writes
a magic/inverse pair at480aef00, watchdog-resets into fresh LK, then copies the
embedded verified stock Image and branches before SEC/DXE. It consumes a
matching request before checking EL1, MMU-off, D-cache-off and little endian.
Previously every rejection silently fell back to UEFI.

The observer uses128 bytes at480aef20..480aef9f, within the already reserved
480ae000 page, after the Android flag and outside the first2KiB bugcheck slot.
Record0 describes the latest early entry. Record1 preserves the latest matched
request across a subsequent normal entry, distinguishing a handoff followed
by an Android kernel reset from a request not accepted at all.
Tag3154554f52414744; fields tag/stage/flag/inverse/CurrentEL/SCTLR_EL1.
Record0+48 retains a request marker written by UEFI. Missing marker means
it too may have been lost; the observer does not assume retention.
Stages:1=UEFI request committed;3=EL rejected;4=MMU/cache/endian rejected;
5=qualified stock-Image handoff;6=missing/torn request at early entry.
The normal UEFI menu logs both records as DEGAS ANDROID entry/last_match.
No guard has been relaxed. GZ/ATF/LK are unchanged. This is instrumentation,
not a claimed repair. EL2 entry is logged without reading SCTLR_EL1 there.

Actual linked-route Unicorn checks passed: stock Image overlapping copy,
boot x0..x3/SP/DAIF preservation, I-cache maintenance sequence, all guards,
missing/torn flag, journal values and preservation of the last-match record.
Unicorn does not prove physical RAM reset retention or hardware cache behavior.

Combined RF observer reads12 source-qualified MT6363 SID4 registers only,
using the established PMIF read helper. No PMIC writes, SMC, power, firmware,
RF or DMA operations. Expected RF SNAPSHOT SAVED. PMIF host transaction
register writes are required for reads; they are not PMIC writes.
The exact Android reference lives in android-mt6363-conn-rails.json under
evidence/raw/2026-10-30-degas-wifi-power.


V5 hardware result: RF SNAPSHOT SAVED; all12 PMIC reads succeeded and Android
booted from the UEFI menu. The logged entry stage6 preceded the user request
on the initial UEFI boot, with uninitialized flag contents; it is expected
and is NOT an observed rejection of that user request. No failing menu boot
has yet been captured with the observer. Therefore the intermittent Android
boot issue remains unproven and no repair is claimed from this successful
attempt. The diagnostic remains enabled in subsequent private images.


V7 fallback: the fixed request slot is not a dependable single transport.
V6's next early entry saw unrelated values; the user confirms Android was
selected, but the original failing console was replaced by a deliberate
second RF test. Exact overwriter/reset phase remains unproved. The console
region itself repeatedly survives the same reset/recovery path.
BootAndroid now appends a16-byte magic/inverse token through SerialPortWrite
AFTER all output/watchdog setup, with IRQs masked to keep it the final record.
Early entry validates the existing console signature/start/size/capacity and
checks the final16 ring bytes using byte accesses (including wrap), consumes
a matching token, and still applies all original entry-context guards.
No console header or capacity changes, new RAM allocation, boot-mode/PMIC
scratch-register writes, slot switches or persistent partition flags.
Primary request remains supported. source1=primary,source2=console fallback.
Both valid requests are consumed before stock Image handoff; a second boot
cannot replay the console token. Ten actual linked-code Unicorn tests passed,
including a fresh-load second boot, wrap/unaligned positions, corrupt token,
invalid header bounds, missing primary, all guards and stock copying/ABI.
This is a candidate fix; hardware confirmation of the failing scenario is
required before claiming reliable Android routing.


V7 hardware failure: user did not repeat RF this time. Retained console has
one P0 entry, the RF pass and timeout arm, but no subsequent BootAndroid
request and no second wrapper entry. Console fallback did not solve the
reported outcome. This is insufficient to declare a specific RAM overwriter
or claim an Android kernel crash. Compare the intentional reboot mechanism.

V8 replaces intentional RF completion/Android reboot waits with the exact
public MT6897 mtk_wdt_restart operation: write1209 to watchdog base+14,
repeat every5ms if necessary. DT base1c00a000; no SWSYSRST18 or reset-bit
masks are written. RF result stays visible3s then the software command is
issued. BootAndroid issues it immediately after publishing the console token,
without any subsequent logging. The reset-only watchdog remains fallback.
Both sites log the intended software command before issuing it. No claims
of a hardware fix before the new path has been observed on the phone.


V8 result: user explicitly observed RF software reboot, fresh menu, Android
selection and return to UEFI without repeating RF. Yet recovered pstore still
contains only one P0 entry and ends at the first RF SWRST command. Therefore
this console is not a reliable complete history of those later boots. The
previous inference of a lost fixed flag, and suspicion that RF reboot did not
occur, are not proved. Neither fallback request nor software reset is a
hardware-qualified fix for the Android return.

V9 adds only a live menu footer; routing/RF/reset behavior is unchanged.
A E=latest-entry stage; M=last matched request stage (9 means invalid record);
S=request source1=primary/2=console; Q=UEFI request marker valid.
ROUTE V9 TICK increments each20 menu polls (~1s plus rendering time).
Read it AFTER the failed Android return, without rerunning RF. This bypasses
assumptions about which pstore generation was recovered and also distinguishes
an actually refreshing menu from a retained framebuffer image.
