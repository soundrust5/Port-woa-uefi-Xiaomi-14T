# Install UEFI on Xiaomi 14T

[Home](../../README.md) | **English** | [Русский](../Russian/installation.md)

> [!CAUTION]
> If you are unsure or afraid to install UEFI, feel free to close this repository. We are not responsible for damaged phones. This experimental port has bugs and unfinished features and may leave your phone unusable.

Only for **Xiaomi 14T (`degas`, MT6897)**, not Xiaomi 14T Pro. This installs the UEFI menu, **not Windows**. Windows partitioning and installation are outside this guide. Compatibility with every Android firmware version has not been verified.

## 1. Prepare your computer and phone

- Back up your personal data outside the phone.
- You need an **already unlocked bootloader**, rooted working Android, USB debugging, and a charged phone. Bootloader unlocking is outside this guide and can erase data.
- Install [Android Platform Tools](https://developer.android.com/tools/releases/platform-tools). Open **Windows PowerShell** in that folder. The commands below use `./adb.exe` and `./fastboot.exe`; do not paste them into Android's terminal.
- Download `boot.img` and `SHA256SUMS` from the [release](https://github.com/soundrust5/Port-woa-uefi-Xiaomi-14T/releases/latest) into the same folder. Keep the backup in a separate safe location too.
- Run each block in order in the **same PowerShell window**. Stop on an error, `FAILED`, an empty result or an unexpected device. Connect only this phone.

## 2. Check the device and its current slot

Connect the phone, allow USB debugging on its screen, and run:

```powershell
./adb.exe devices
./adb.exe shell getprop ro.product.device
./adb.exe shell su -c id
```

Expect one authorized `device`, product `degas`, and root `uid=0`. Accept the root request on the phone if prompted.

A/B slots are two sets of Android boot partitions. **Your active slot may be A or B.** B was used on our test phone; it is not a universal requirement. The following commands read your slot and remember it:

```powershell
$slot = (./adb.exe shell getprop ro.boot.slot_suffix).Trim().TrimStart('_')
if ($slot -notin @('a', 'b')) { throw 'Cannot identify the Android slot. Stop.' }
$partition = "boot_$slot"
$slot | Set-Content -Encoding ascii ./original-slot.txt
Write-Output "Android slot: $slot; target partition: $partition"
```

Do not change slots to match someone else's instructions, and do not perform an Android OTA update during this procedure.

## 3. Back up your working Android boot image

These commands copy your current boot partition; they do not write to it. If it already contains UEFI, use your original Android backup instead. A rooted Android backup may include your root modifications.

```powershell
./adb.exe shell "su -c 'dd if=/dev/block/by-name/$partition of=/data/local/tmp/android-$partition.img bs=4M'"
./adb.exe shell "su -c 'chmod 0644 /data/local/tmp/android-$partition.img'"
./adb.exe shell "su -c 'sha256sum /dev/block/by-name/$partition'"
./adb.exe pull "/data/local/tmp/android-$partition.img" "./android-$partition.img"
Get-FileHash "./android-$partition.img" -Algorithm SHA256
Get-FileHash ./boot.img -Algorithm SHA256
```

The backup hash must match the partition hash printed by `sha256sum`. The UEFI `boot.img` hash must match its line in `SHA256SUMS` (letter case does not matter). **Do not continue without a verified backup.** Save `original-slot.txt` and the backup somewhere outside the phone.

## 4. Enter fastboot and check the slot again

```powershell
./adb.exe reboot bootloader
./fastboot.exe devices
./fastboot.exe getvar product
./fastboot.exe getvar current-slot
```

Expect one phone, product `degas`, and the **same slot saved in step 2**. If the slot differs, stop; do not force it to A or B. Use bootloader fastboot, not recovery's fastbootd.

## 5. Flash the checked slot

This replaces only the selected boot partition. It does not format the phone or install Windows.

```powershell
./fastboot.exe flash $partition ./boot.img
```

Wait for `OKAY` and successful completion. Only then run:

```powershell
./fastboot.exe reboot
```

The UEFI menu should appear. Volume buttons move the selection; Power confirms. `BOOT ANDROID` requests an automatic restart into Android; let it restart without holding buttons. `WINDOWS BOOT MANAGER` requires an already prepared compatible Windows installation. Diagnostic menu items are not installation steps.

## 6. Restore Android if needed

Hold **Power + Volume Down** to enter fastboot. Open PowerShell in the folder containing your backup and `original-slot.txt`:

```powershell
$slot = (Get-Content ./original-slot.txt -Raw).Trim()
if ($slot -notin @('a', 'b')) { throw 'Invalid saved slot. Stop.' }
$partition = "boot_$slot"
./fastboot.exe devices
./fastboot.exe getvar product
Get-FileHash "./android-$partition.img" -Algorithm SHA256
```

Confirm product `degas` and the original verified backup hash. Restore it:

```powershell
./fastboot.exe flash $partition "./android-$partition.img"
```

Only after the flash succeeds, select your original slot and restart:

```powershell
./fastboot.exe set_active $slot
./fastboot.exe reboot
```

Do not erase other partitions, relock the bootloader, or use another phone's backup.

Currently this is a **UEFI installation guide only**. See [component status](status.md) for Windows support.
