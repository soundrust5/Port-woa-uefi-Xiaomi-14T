<img align="right" src="assets/xiaomi-14t.png" width="300" alt="Xiaomi 14T">

# Windows 11 ARM64 и UEFI на Xiaomi 14T

[English](README.md) | **Русский**

Экспериментальный порт Windows on ARM (WoA) для **Xiaomi 14T (`degas`) — MediaTek Dimensity 8300-Ultra (MT6897)**.

Windows 11 PRO загружается с UFS. Работают CPU, дисплей, сенсорный экран с мультитачем, чтение и запись UFS. Аппаратные кнопки работают только в UEFI.

> [!IMPORTANT]
> Публичная установка пока недоступна.

- [Статус проекта](guide/Russian/status.md)
- [Установка](guide/Russian/installation.md)
- [Исходники Mu-Silicium](mu/) · [Сведения об исходниках и лицензиях](SOURCE.md)

Разработчик: [soundrust5](https://github.com/soundrust5).

<br clear="both">


## Исходники и экспериментальный выпуск — 29 октября 2026

Опубликованы актуальные исходники UEFI, Windows-драйверов UFS/тача, CPU/DVFS-исследований, скрипты сборки и отчёты. [Состав и восстановление зависимостей](SOURCE.md) · [Выпуски UEFI](../../releases).

На устройстве ранее проверены Windows с 8 ядрами, UFS read/write, мультитач и автономное обновление framebuffer. DVFS ещё не работает; GPU, Wi-Fi и Bluetooth не реализованы. Новый fullscreen-образ прошёл сборку и проверку упаковки, но именно этот бинарник ещё не запускался на телефоне. Windows и сторонние бинарники в исходники не входят.
