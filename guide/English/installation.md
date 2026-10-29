# UEFI installation

[Home](../../README.md) | **English** | [Русский](../Russian/installation.md)

> [!CAUTION]
> If you are unsure or afraid to install UEFI on your phone, feel free to close this repository. We are not responsible for damaged phones. This port is experimental, with many bugs and unfinished features; installation may leave your device unusable.

For **Xiaomi 14T (`degas`, MT6897) only**. Xiaomi 14T Pro and other devices are unsupported. This guide installs UEFI into `boot_b`; it does not install Windows or prepare Windows partitions.

## Before you start

- An already unlocked bootloader, a charged phone, a USB cable and a computer with ADB/Fastboot.
- Working fastboot access and a copy of your data outside the phone.
- Your own verified original **stock `boot_b` backup**, saved on the computer. If `boot_b` already contains UEFI, use the stock backup made before that installation.
- Read the [release notes and download the experimental `boot.img` and `SHA256SUMS`](https://github.com/soundrust5/Port-woa-uefi-Xiaomi-14T/releases/tag/experimental-2026-10-29-fullscreen-8cpu). This exact fullscreen image is build/package-qualified; it has not yet been booted on the phone.

## 1. Back up stock boot_b

From **rooted stock Android**, with USB debugging enabled and your computer authorized:

```sh
adb shell su -c 'dd if=/dev/block/by-name/boot_b of=/data/local/tmp/stock-boot_b.img bs=4M'
adb shell su -c 'chmod 0644 /data/local/tmp/stock-boot_b.img'
adb shell su -c 'sha256sum /dev/block/by-name/boot_b'
adb pull /data/local/tmp/stock-boot_b.img ./stock-boot_b.img
```

On Windows PowerShell, verify that the backup hash matches the partition hash printed above. Keep this backup separate from the downloaded UEFI image:

```powershell
Get-FileHash .\stock-boot_b.img -Algorithm SHA256
Get-FileHash .\boot.img -Algorithm SHA256
```

The downloaded `boot.img` hash must match its entry in `SHA256SUMS`. If you cannot obtain and verify your stock backup, stop here.

## 2. Flash UEFI

Enter bootloader fastboot. Check that the connected phone is your Xiaomi 14T and record its original active slot:

```sh
adb reboot bootloader
fastboot devices
fastboot getvar product
fastboot getvar current-slot
```

Continue only when the product is `degas`. Run these commands from the folder containing the verified UEFI `boot.img`:

```sh
fastboot flash boot_b boot.img
fastboot set_active b
fastboot reboot
```

The UEFI boot menu should appear. Windows boot requires a separately prepared compatible installation and drivers; this image alone cannot install Windows. The experimental `CPU_DVFS_SNAPSHOT` route performs a manual 2 GHz test and is not needed for installation.

## 3. Restore stock boot_b

If the device fails to boot, hold **Power + Volume Down** to return to fastboot, connect USB and restore **your own verified stock backup**:

```sh
fastboot devices
fastboot flash boot_b stock-boot_b.img
fastboot set_active b
fastboot reboot
```

Do not erase partitions, relock the bootloader or flash another device's backup. If your previous Android setup used slot A, you can select that original slot with `fastboot set_active a` after restoring `boot_b`.

[Project status](status.md)
