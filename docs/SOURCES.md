# Источники

Проверены 2026-09-14. Это отправная точка, не полный аудит платформы. Локальные сведения отделены от веб-источников в `DEVICE.md`.

- **S1 — Xiaomi, спецификация Xiaomi 14T:** https://www.mi.com/pk/product/xiaomi-14t/specs/ — SoC и GPU. Не описывает регистры или контракт загрузки.
- **S2 — MiCode, перечень исходников:** https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/README/README.md?plain=1 — запись Xiaomi 14T / bsp-degas-u-oss / Android U. Целевая ветка: https://github.com/MiCode/Xiaomi_Kernel_OpenSource/tree/bsp-degas-u-oss . Само дерево ещё не изучено; при открытии ветки браузерный поиск вернул ошибку, наличие записи подтверждено в перечне.
- **S3 — linux-mediatek/edk2-mtk:** https://github.com/linux-mediatek/edk2-mtk — пример MediaTek-порта; архивирован 2025-10-04, degas не указан среди поддерживаемых устройств в проверенном README. Это не утверждение об отсутствии любых других портов в интернете.
- **S4 — AOSP, Boot image header:** https://source.android.com/docs/core/architecture/bootloader/boot-image-header — структуры версий загрузочного контейнера; версия локальных образов установлена чтением байтов заголовков.
- **S5 — Linux, Booting AArch64 Linux:** https://docs.kernel.org/arch/arm64/booting.html — стандартный контракт входа в ARM64 Linux. Его соблюдение нашим загрузчиком требует проверки, а требования EDK2 рассматриваются отдельно.
- **S6 — TianoCore EDK II:** https://github.com/tianocore/edk2 — исходная среда UEFI/PI и ArmVirtPkg; не готовый пакет Xiaomi 14T.
- **S7 — Microsoft, ACPI system description tables:** https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-system-description-tables — роль таблиц описания платформы для Windows.
- **S8 — Microsoft, UEFI requirements for Windows editions on SoC platforms:** https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/uefi-requirements-that-apply-to-all-windows-platforms — интерфейсы firmware/ОС; текст явно содержит требования старых поколений Windows/ARM. Не использовать как единственный актуальный набор требований Windows 11 ARM64.
- **S9 — Microsoft, Windows on Arm:** https://learn.microsoft.com/en-us/windows/arm/overview — драйверы оборудования должны поддерживать Arm64; эмуляция приложений не заменяет поддержку драйверов.

- **S10 — Project-Silicium/Mu-Silicium:** https://github.com/Project-Silicium/Mu-Silicium — кандидат на базу и источник платформенного кода. Через GitHub API проверено полное дерево main, commit `964a886f3d1ad3f35137435b11287c8d6827437b` (`truncated=false`): есть `Silicon/MediaTek/MT6768Pkg`, `MT6789Pkg`, `MediaTekPkg`, нет путей с degas или 6897. Наличие файлов не является подтверждением работоспособности соответствующих драйверов на нашем устройстве. Дерево: https://github.com/Project-Silicium/Mu-Silicium/tree/964a886f3d1ad3f35137435b11287c8d6827437b/Silicon/MediaTek .
- **S11 — Microsoft Project Mu:** https://microsoft.github.io/mu/ — модульная адаптация TianoCore EDK2.

Следующее исследование должно сравнить базовые реализации, получить точные параметры конкретной платы и проверить актуальные требования выбранной сборки Windows 11. Пока отсутствуют доказательства поддержки Windows на этом устройстве и подтверждённые адреса аппаратных регистров для нашего кода.
