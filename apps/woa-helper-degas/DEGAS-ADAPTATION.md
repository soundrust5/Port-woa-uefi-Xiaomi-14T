# WoA Helper — Xiaomi 14T (degas)

Android companion adapted from the existing OnePlus 12 fork of n00b69/woa-helper. Package: `com.woa.helper.degas`.

## Русский

Нужен root (KernelSU/Magisk). Приложение поддерживает только `degas`.

- Подключение раздела Windows C: в `/storage/emulated/0/Windows` или `/mnt/Windows`.
- Отдельный выбор «только чтение» и «чтение/запись». Плитка, виджет и автомонтирование используют только чтение.
- Для записи полностью завершите работу Windows. Грязный/гибернированный NTFS не восстанавливается принудительно.
- Перезагрузка в установленное меню UEFI, резервная копия boot активного слота.
- Русский и английский интерфейс. Прошивка разделов, модема и devcfg отключена.

Раздел находится по `/dev/block/by-name/Windows`, без фиксированного номера UFS или слота. SELinux не отключается. В обычном файловом менеджере доступ зависит от разрешений Android и видимости mount namespace; для окончательной проверки нужен тест UI на конкретной прошивке.

## English

Root (KernelSU/Magisk) is required. Only `degas` is supported. Mount the named Windows partition read-only or read/write; browse it at the paths above. Fully shut down Windows before writable access. Dirty/hibernated NTFS is never forced writable. Widgets/tiles/automount are read-only. Boot backup resolves the active slot dynamically; reboot enters the installed UEFI menu. Partition flashing, modem/devcfg and legacy boot patching remain disabled. SELinux is unchanged.

## Build / validation

JDK 21, Android SDK 36, bundled Gradle wrapper:

```powershell
.\gradlew.bat testDebugUnitTest assembleDebug
```

Debug APK is signed with the local Android debug certificate. Unit tests and APK build passed. Native RO mount and RW temporary-file readback/removal/unmount passed on the phone in the global root mount namespace, using the same NTFS binary. The user confirmed reading and writing through the installed app. The quickboot action now explicitly unmounts a verified Windows volume before reboot and cancels reboot on unmount failure; that reboot behavior has build validation, not a new device reboot test.

The native executable is `app/src/main/jniLibs/arm64-v8a/libntfs3g_exec.so`, SHA256 `77f47b1b5655a385c136d1b6bc0b0e1cd15f43446bb89886aa7b6c7a34b29a15`. It is the already-tested static ARM64 NTFS-3G build. Matching source archive and COPYING are in `third_party/`; root repository script `tools/degas_full_windows_build_ntfs.sh` records its build recipe. Preserved upstream README and OnePlus adaptation notes describe ancestry, not the current supported-device list. No new license is asserted over upstream code.
