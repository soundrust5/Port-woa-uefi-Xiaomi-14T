# WoA Helper для Xiaomi 14T

## Русский

Нужны Xiaomi 14T, root и уже установленная Windows.

1. [Скачайте APK](https://github.com/soundrust5/Port-woa-uefi-Xiaomi-14T/releases/download/experimental-2026-10-30-android-efi-adie/WoA-Helper-Xiaomi-14T.apk), установите его и разрешите root при первом запуске.
2. Нажмите подключение Windows и выберите чтение или чтение/запись.
3. Откройте Windows в файловом менеджере. Чтобы закончить работу или сменить режим, отключите её в приложении.
4. Кнопка перезагрузки открывает установленное меню UEFI. Выберите там Android или Windows.

Перед записью полностью завершите работу Windows: после гибернации писать нельзя. Приложение не устанавливает Windows или UEFI.

## English

Requires Xiaomi 14T, root and an existing Windows installation.

1. [Download the APK](https://github.com/soundrust5/Port-woa-uefi-Xiaomi-14T/releases/download/experimental-2026-10-30-android-efi-adie/WoA-Helper-Xiaomi-14T.apk), install it and grant root on first launch.
2. Tap the Windows mount action and choose read-only or read/write.
3. Open Windows in your file manager. Unmount it in the app when finished or before changing access mode.
4. The reboot action opens the installed UEFI menu. Choose Android or Windows there.

Fully shut down Windows before writing; do not write to a hibernated volume. The app does not install Windows or UEFI.

Based on [n00b69/woa-helper](https://github.com/n00b69/woa-helper) through the existing OnePlus 12 adaptation. Upstream notices and licenses still apply; see [ancestry notes](UPSTREAM-OP12-ADAPTATION.md) and [NTFS-3G license](third_party/NTFS-3G-COPYING).
