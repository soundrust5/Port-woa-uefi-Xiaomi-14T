# Установка UEFI на Xiaomi 14T

[Главная](../../README.ru-RU.md) | [English](../English/installation.md) | **Русский**

> [!CAUTION]
> Если вы сомневаетесь или боитесь устанавливать UEFI — смело закрывайте этот репозиторий. Мы не несём ответственности за повреждённые телефоны. В экспериментальном порте есть ошибки и недоработки; установка может привести к неработоспособности телефона.

Только для **Xiaomi 14T (`degas`, MT6897)**, не для Xiaomi 14T Pro. Инструкция устанавливает меню UEFI, **а не Windows**. Разметка накопителя и установка Windows сюда не входят. Совместимость со всеми версиями прошивки Android не проверена.

## 1. Подготовьте компьютер и телефон

- Сохраните личные данные вне телефона.
- Нужны **уже разблокированный загрузчик**, рабочий Android с root, USB-отладка и заряженный телефон. Разблокировка загрузчика в эту инструкцию не входит и может стереть данные.
- Установите [Android Platform Tools](https://developer.android.com/tools/releases/platform-tools). Откройте **Windows PowerShell** в этой папке. Команды ниже используют `./adb.exe` и `./fastboot.exe`; вводить их в терминале телефона не нужно.
- Скачайте `boot.img` и `SHA256SUMS` из [релиза](https://github.com/soundrust5/Port-woa-uefi-Xiaomi-14T/releases/latest) в ту же папку. Резервную копию дополнительно храните в отдельном безопасном месте.
- Выполняйте блоки по порядку в **одном окне PowerShell**. При ошибке, `FAILED`, пустом результате или неожиданном устройстве остановитесь. Подключите только этот телефон.

## 2. Проверьте устройство и текущий слот

Подключите телефон, разрешите USB-отладку на его экране и выполните:

```powershell
./adb.exe devices
./adb.exe shell getprop ro.product.device
./adb.exe shell su -c id
```

Ожидается один телефон со статусом `device`, продукт `degas` и root `uid=0`. При необходимости подтвердите запрос root на телефоне.

Слоты A/B — два набора загрузочных разделов Android. **У вас активным может быть и A, и B.** На нашем тестовом телефоне был B, но это не общее правило. Команды ниже определят ваш слот и сохранят его:

```powershell
$slot = (./adb.exe shell getprop ro.boot.slot_suffix).Trim().TrimStart('_')
if ($slot -notin @('a', 'b')) { throw 'Cannot identify the Android slot. Stop.' }
$partition = "boot_$slot"
$slot | Set-Content -Encoding ascii ./original-slot.txt
Write-Output "Android slot: $slot; target partition: $partition"
```

Не переключайте слот ради совпадения с чужим примером. Во время этой процедуры не устанавливайте OTA-обновления Android.

## 3. Сохраните рабочий загрузочный образ Android

Команды копируют текущий раздел boot, ничего в него не записывая. Если там уже стоит UEFI, используйте прежнюю копию Android. Копия с рутированного Android может содержать ваши изменения для root.

```powershell
./adb.exe shell "su -c 'dd if=/dev/block/by-name/$partition of=/data/local/tmp/android-$partition.img bs=4M'"
./adb.exe shell "su -c 'chmod 0644 /data/local/tmp/android-$partition.img'"
./adb.exe shell "su -c 'sha256sum /dev/block/by-name/$partition'"
./adb.exe pull "/data/local/tmp/android-$partition.img" "./android-$partition.img"
Get-FileHash "./android-$partition.img" -Algorithm SHA256
Get-FileHash ./boot.img -Algorithm SHA256
```

Хеш резервной копии должен совпасть с хешем раздела, который вывела команда `sha256sum`. Хеш UEFI `boot.img` должен совпасть с его строкой в `SHA256SUMS` — регистр букв не важен. **Без проверенной копии дальше не продолжайте.** Сохраните `original-slot.txt` и копию образа вне телефона.

## 4. Перейдите в fastboot и снова проверьте слот

```powershell
./adb.exe reboot bootloader
./fastboot.exe devices
./fastboot.exe getvar product
./fastboot.exe getvar current-slot
```

Ожидается один телефон, продукт `degas` и **тот же слот, который сохранён на шаге 2**. Если слот отличается — остановитесь, не назначайте A или B принудительно. Нужен fastboot загрузчика, а не fastbootd из recovery.

## 5. Прошейте проверенный слот

Команда заменит только выбранный раздел boot. Она не форматирует телефон и не устанавливает Windows.

```powershell
./fastboot.exe flash $partition ./boot.img
```

Дождитесь `OKAY` и успешного завершения. Только после этого выполните:

```powershell
./fastboot.exe reboot
```

Должно появиться меню UEFI. Громкость перемещает выбор, Power подтверждает. `BOOT ANDROID` запрашивает автоматический перезапуск в Android — дождитесь его, не удерживая кнопки. Для `WINDOWS BOOT MANAGER` нужна заранее подготовленная совместимая установка Windows. Диагностические пункты меню для установки не нужны.

## 6. При необходимости восстановите Android

Удерживайте **Power + уменьшение громкости**, чтобы войти в fastboot. Откройте PowerShell в папке с резервной копией и `original-slot.txt`:

```powershell
$slot = (Get-Content ./original-slot.txt -Raw).Trim()
if ($slot -notin @('a', 'b')) { throw 'Invalid saved slot. Stop.' }
$partition = "boot_$slot"
./fastboot.exe devices
./fastboot.exe getvar product
Get-FileHash "./android-$partition.img" -Algorithm SHA256
```

Убедитесь, что продукт — `degas`, а хеш совпадает с хешем вашей проверенной копии. Восстановите её:

```powershell
./fastboot.exe flash $partition "./android-$partition.img"
```

Только после успешной прошивки выберите исходный слот и перезагрузитесь:

```powershell
./fastboot.exe set_active $slot
./fastboot.exe reboot
```

Не стирайте другие разделы, не блокируйте загрузчик обратно и не используйте копию с другого телефона.

Сейчас это **только инструкция по установке UEFI**. Поддержка компонентов Windows указана в [статусе проекта](status.md).
