# Install UEFI on Xiaomi 14T

[Home](../../README.md) | **English** | [Русский](../Russian/installation.md)

> [!CAUTION]
> Install at your own risk. This unfinished port can leave your phone unusable. We are not responsible for damaged devices.

Only for **Xiaomi 14T (`degas`, MT6897)**, not the Pro. This installs UEFI only. Windows installation is not covered here. Compatibility with every Android firmware version is unverified.

## 1. Prepare your computer and phone

- Back up your data outside the phone.
- You need an **unlocked bootloader**, working rooted Android, USB debugging and a charged phone. Unlocking is not covered here and can erase data.
- Install [Android Platform Tools](https://developer.android.com/tools/releases/platform-tools) and open **Windows PowerShell** in that folder. Run the commands on your computer.
- Download `boot.img` and `SHA256SUMS` from the [release](https://github.com/soundrust5/Port-woa-uefi-Xiaomi-14T/releases/latest) into that folder.
- Connect only this phone. Run the blocks in order in the **same PowerShell window**. Stop on errors, `FAILED` or unexpected/empty results.

## 2. Check the device and its current slot

Allow USB debugging on the phone, then run:

```powershell
./adb.exe devices
./adb.exe shell getprop ro.product.device
./adb.exe shell su -c id
```

Expect one `device`, product `degas` and root `uid=0`. Grant root on the phone if prompted.

**Your active slot may be A or B.** Read and save it:

```powershell
$slot = (./adb.exe shell getprop ro.boot.slot_suffix).Trim().TrimStart('_')
if ($slot -notin @('a', 'b')) { throw 'Cannot identify the Android slot. Stop.' }
$partition = "boot_$slot"
$slot | Set-Content -Encoding ascii ./original-slot.txt
Write-Output "Android slot: $slot; target partition: $partition"
```

Keep your current slot. Do not install Android OTA updates during this procedure.

## 3. Back up your working Android boot image

If UEFI is already installed, use your original Android backup. Otherwise, copy the current boot image:

```powershell
./adb.exe shell "su -c 'dd if=/dev/block/by-name/$partition of=/data/local/tmp/android-$partition.img bs=4M'"
./adb.exe shell "su -c 'chmod 0644 /data/local/tmp/android-$partition.img'"
./adb.exe shell "su -c 'sha256sum /dev/block/by-name/$partition'"
./adb.exe pull "/data/local/tmp/android-$partition.img" "./android-$partition.img"
Get-FileHash "./android-$partition.img" -Algorithm SHA256
Get-FileHash ./boot.img -Algorithm SHA256
```

Match the backup hash to `sha256sum`, and the UEFI image hash to `SHA256SUMS`; letter case does not matter. **Do not continue without a verified backup.** Keep it and `original-slot.txt` outside the phone.

## 4. Enter fastboot and check the slot again

```powershell
./adb.exe reboot bootloader
./fastboot.exe devices
./fastboot.exe getvar product
./fastboot.exe getvar current-slot
```

Check for one phone, product `degas` and **the slot saved in step 2**. Stop if it differs. Use bootloader fastboot, not fastbootd.

## 5. Flash the checked slot

Flash only the checked boot partition:

```powershell
./fastboot.exe flash $partition ./boot.img
```

Wait for `OKAY` and successful completion, then reboot:

```powershell
./fastboot.exe reboot
```

In UEFI, use Volume to select and Power to confirm. Choose `BOOT ANDROID` for Android. Windows Boot Manager needs an existing compatible Windows installation. Skip diagnostics during installation.

## 6. Restore Android if needed

Hold **Power + Volume Down** for fastboot. Open PowerShell beside your backup and `original-slot.txt`:

```powershell
$slot = (Get-Content ./original-slot.txt -Raw).Trim()
if ($slot -notin @('a', 'b')) { throw 'Invalid saved slot. Stop.' }
$partition = "boot_$slot"
./fastboot.exe devices
./fastboot.exe getvar product
Get-FileHash "./android-$partition.img" -Algorithm SHA256
```

Confirm product `degas` and your original backup hash, then restore:

```powershell
./fastboot.exe flash $partition "./android-$partition.img"
```

After a successful flash, select your saved slot and restart:

```powershell
./fastboot.exe set_active $slot
./fastboot.exe reboot
```

Do not erase other partitions, relock the bootloader, or use another phone's backup.

See [component status](status.md) for Windows support.
