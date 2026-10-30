# Установка UEFI на Xiaomi 14T

[Главная](../../README.ru-RU.md) | [English](../English/installation.md) | **Русский**

> [!CAUTION]
> Устанавливайте на свой риск. Порт ещё не закончен и может привести к неработоспособности телефона. Мы не несём ответственности за повреждённые устройства.

Только для **Xiaomi 14T (`degas`, MT6897)**, не для Pro. Здесь описана только установка UEFI. Установка Windows здесь не описана. Совместимость со всеми прошивками Android не проверена.

## 1. Подготовьте компьютер и телефон

- Сохраните данные вне телефона.
- Нужны **разблокированный загрузчик**, рабочий Android с root, USB-отладка и заряженный телефон. Разблокировка здесь не описана и может стереть данные.
- Установите [Android Platform Tools](https://developer.android.com/tools/releases/platform-tools) и откройте **Windows PowerShell** в этой папке. Команды выполняются на компьютере.
- Скачайте `boot.img` и `SHA256SUMS` из [релиза](https://github.com/soundrust5/Port-woa-uefi-Xiaomi-14T/releases/latest) в ту же папку.
- Подключите только этот телефон. Выполняйте блоки по порядку в **одном окне PowerShell**. При ошибке, `FAILED`, пустом или неожиданном результате остановитесь.

## 2. Проверьте устройство и текущий слот

Разрешите USB-отладку на телефоне и выполните:

```powershell
./adb.exe devices
./adb.exe shell getprop ro.product.device
./adb.exe shell su -c id
```

Ожидается один `device`, продукт `degas` и root `uid=0`. При необходимости разрешите root на телефоне.

**Активным может быть слот A или B.** Определите и сохраните его:

```powershell
$slot = (./adb.exe shell getprop ro.boot.slot_suffix).Trim().TrimStart('_')
if ($slot -notin @('a', 'b')) { throw 'Cannot identify the Android slot. Stop.' }
$partition = "boot_$slot"
$slot | Set-Content -Encoding ascii ./original-slot.txt
Write-Output "Android slot: $slot; target partition: $partition"
```

Сохраните текущий слот. Не устанавливайте OTA-обновления Android во время этой процедуры.

## 3. Сохраните рабочий загрузочный образ Android

Если UEFI уже установлен, используйте прежнюю копию Android. Иначе сохраните текущий boot:

```powershell
./adb.exe shell "su -c 'dd if=/dev/block/by-name/$partition of=/data/local/tmp/android-$partition.img bs=4M'"
./adb.exe shell "su -c 'chmod 0644 /data/local/tmp/android-$partition.img'"
./adb.exe shell "su -c 'sha256sum /dev/block/by-name/$partition'"
./adb.exe pull "/data/local/tmp/android-$partition.img" "./android-$partition.img"
Get-FileHash "./android-$partition.img" -Algorithm SHA256
Get-FileHash ./boot.img -Algorithm SHA256
```

Сверьте хеш копии с `sha256sum`, а хеш UEFI — с `SHA256SUMS`; регистр букв не важен. **Без проверенной копии не продолжайте.** Храните её и `original-slot.txt` вне телефона.

## 4. Перейдите в fastboot и снова проверьте слот

```powershell
./adb.exe reboot bootloader
./fastboot.exe devices
./fastboot.exe getvar product
./fastboot.exe getvar current-slot
```

Проверьте один телефон, продукт `degas` и **слот с шага 2**. Если слот отличается, остановитесь. Нужен fastboot загрузчика, не fastbootd.

## 5. Прошейте проверенный слот

Прошейте только проверенный раздел boot:

```powershell
./fastboot.exe flash $partition ./boot.img
```

Дождитесь `OKAY` и успешного завершения, затем перезагрузитесь:

```powershell
./fastboot.exe reboot
```

В UEFI выбирайте пункт кнопками громкости и подтверждайте кнопкой питания. Для Android выберите `BOOT ANDROID`. Windows Boot Manager требует готовой совместимой установки Windows. Диагностику при установке пропустите.

## 6. При необходимости восстановите Android

Удерживайте **Power + уменьшение громкости** для входа в fastboot. Откройте PowerShell рядом с копией и `original-slot.txt`:

```powershell
$slot = (Get-Content ./original-slot.txt -Raw).Trim()
if ($slot -notin @('a', 'b')) { throw 'Invalid saved slot. Stop.' }
$partition = "boot_$slot"
./fastboot.exe devices
./fastboot.exe getvar product
Get-FileHash "./android-$partition.img" -Algorithm SHA256
```

Проверьте продукт `degas` и хеш исходной копии, затем восстановите её:

```powershell
./fastboot.exe flash $partition "./android-$partition.img"
```

После успешной прошивки выберите сохранённый слот и перезагрузитесь:

```powershell
./fastboot.exe set_active $slot
./fastboot.exe reboot
```

Не стирайте другие разделы, не блокируйте загрузчик обратно и не используйте копию с другого телефона.

Поддержка Windows указана в [статусе проекта](status.md).
