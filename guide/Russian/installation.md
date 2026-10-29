# Установка UEFI

[Главная](../../README.ru-RU.md) | [English](../English/installation.md) | **Русский**

> [!CAUTION]
> Если вы сомневаетесь или боитесь устанавливать UEFI на свой телефон — смело закрывайте этот репозиторий. Мы не несём ответственности за повреждённые телефоны. Порт экспериментальный: в нём много ошибок и недоработок; установка может привести к неработоспособности устройства.

Только для **Xiaomi 14T (`degas`, MT6897)**. Xiaomi 14T Pro и другие устройства не поддерживаются. Этот гайд устанавливает UEFI в `boot_b`; установка Windows и подготовка разделов Windows в него не входят.

## Перед началом

- Уже разблокированный загрузчик, заряженный телефон, USB-кабель и компьютер с ADB/Fastboot.
- Рабочий доступ к fastboot и копия личных данных вне телефона.
- Собственная проверенная резервная копия **оригинального стокового `boot_b`** на компьютере. Если в `boot_b` уже стоит UEFI, используйте стоковую копию, сделанную до его установки.
- Прочитайте [описание экспериментального выпуска и скачайте `boot.img` и `SHA256SUMS`](https://github.com/soundrust5/Port-woa-uefi-Xiaomi-14T/releases/tag/experimental-2026-10-29-fullscreen-8cpu). Новый полноэкранный образ прошёл сборку и проверку упаковки, но именно этот бинарник ещё не запускался на телефоне.

## 1. Сохраните стоковый boot_b

В **стоковом Android с root**, включённой USB-отладкой и разрешённым доступом компьютера:

```sh
adb shell su -c 'dd if=/dev/block/by-name/boot_b of=/data/local/tmp/stock-boot_b.img bs=4M'
adb shell su -c 'chmod 0644 /data/local/tmp/stock-boot_b.img'
adb shell su -c 'sha256sum /dev/block/by-name/boot_b'
adb pull /data/local/tmp/stock-boot_b.img ./stock-boot_b.img
```

В Windows PowerShell проверьте, что хеш копии совпадает с выведенным выше хешем раздела. Храните эту копию отдельно от скачанного UEFI:

```powershell
Get-FileHash .\stock-boot_b.img -Algorithm SHA256
Get-FileHash .\boot.img -Algorithm SHA256
```

Хеш скачанного `boot.img` должен совпадать со строкой для него в `SHA256SUMS`. Если сохранить и проверить стоковую копию не удалось, остановитесь здесь.

## 2. Прошейте UEFI

Перейдите в fastboot загрузчика. Проверьте, что подключён ваш Xiaomi 14T, и запишите исходный активный слот:

```sh
adb reboot bootloader
fastboot devices
fastboot getvar product
fastboot getvar current-slot
```

Продолжайте только при значении продукта `degas`. Выполните команды из папки с проверенным образом UEFI `boot.img`:

```sh
fastboot flash boot_b boot.img
fastboot set_active b
fastboot reboot
```

Должно появиться загрузочное меню UEFI. Для запуска Windows нужны отдельно подготовленная совместимая установка и драйверы; один этот образ Windows не устанавливает. Экспериментальный пункт `CPU_DVFS_SNAPSHOT` выполняет ручной тест на 2 ГГц и для установки не нужен.

## 3. Восстановите стоковый boot_b

Если телефон не загружается, удерживайте **питание + уменьшение громкости**, чтобы вернуться в fastboot, подключите USB и восстановите **собственную проверенную стоковую копию**:

```sh
fastboot devices
fastboot flash boot_b stock-boot_b.img
fastboot set_active b
fastboot reboot
```

Не стирайте разделы, не блокируйте загрузчик обратно и не прошивайте чужую резервную копию. Если ваш Android до установки работал со слота A, после восстановления `boot_b` можно выбрать исходный слот командой `fastboot set_active a`.

[Статус проекта](status.md)
