# Android menu and cache-control result

Hardware cache visibility control passed: baseline A, dirty physical read A,
post-writeback physical read B; CPU1 OFF confirmed, AP status0. Evidence:
`evidence/raw/2026-10-28-degas-cache-visibility-result/console-ramoops-0`.
This proves that the MMU-off physical observer can miss BSP dirty WB data.
It does not identify or fix the Windows HAL failure.

BOOT ANDROID replaces the temporary RAM JOURNAL TEST menu entry. It commits
an aligned magic/complement at 0x480AEF00, outside the journal's published data,
then uses the known watchdog reset. Fresh LK reloads the composite image; its
early route consumes the request before any C/display/SEC work. A relocated
trampoline copies the verified stock Image and its zero-filled footprint back
to 0x40000000, preserving X0..X3, SP and DAIF, with instruction-cache barriers.
Next boot returns to UEFI because the request has been consumed.

**All future images exposing this menu must use the composite packaging step:**

1. build_degas_first_dxe.sh and package_degas_first_dxe.py (intermediate UEFI).
2. package_degas_dualboot.py intermediate/kernel.bin.gz final-directory.
3. package_degas_boot.py using final-directory/kernel.bin.gz.
4. package_degas_stock_overlay.py to preserve the stock boot partition tail.

Do not flash the intermediate UEFI-only image with this menu: it does not
contain the early route or stock kernel. The stock Image is pinned by SHA256
dbb1b31dfc4cc1beee2bd4600591cf5420d39969b5ab142b9eeb3ef2e442668a.

Initial test image: artifacts/degas-android-dualboot-v1/boot.img,
SHA256 2e12f7b877cc7e64cc34154fe0f607d2ea510482581b5aba89a23ef9fae3c814.
Host emulator tests passed. The user confirmed Android boot on the phone.
ADB reappeared and the retained console includes the one-shot request marker:
`evidence/raw/2026-10-28-degas-android-menu-result/`. Full AP-journal survival
through this composite route still needs a capture containing a committed AP
record; the previous test record was not present in this menu-only attempt.
