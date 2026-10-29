# Требования Mu-Silicium к данным платформы Xiaomi 14T (degas / MT6897)

Дата проверки: 2026-09-14. База анализа: Project-Silicium/Mu-Silicium, commit [`964a886f3d1ad3f35137435b11287c8d6827437b`](https://github.com/Project-Silicium/Mu-Silicium/tree/964a886f3d1ad3f35137435b11287c8d6827437b).

Дополнение реализации: исходная ревизия клонирована в `mu/`. Локальные patches исправляют передачу StackBase, добавляют независимый pitch и убирают безусловную очистку всего display carveout в SEC. Ниже описан исходный аудит; состояние кода и ограничения проверки — в [MU-STRIDE-IMPLEMENTATION.md](MU-STRIDE-IMPLEMENTATION.md) и [diagnostic/README.md](../diagnostic/README.md). Модули и контрольный FD stone собраны; это не пакет degas. См. [BUILD-STATUS.md](BUILD-STATUS.md).

Этот документ определяет, какие факты нужно получить до создания `degasPkg`. Он не задаёт адресов MT6897. Значения из MT6768/MT6789 и готовых пакетов устройств ниже служат только указателями на поля, которые потребуются новой платформе.

## Вывод по архитектуре базы

Путь ранней загрузки в выбранной ревизии выглядит так:

`Android boot container / другой носитель` → необязательный `BootShim` → FD по `PcdFdBaseAddress` → `_ModuleEntryPoint` → стек из PCD → `SecEntry` → статическая карта `MemoryMapLib` → HOB и MMU → распаковка FV → DXE Core → `RamManagerDxe` → BDS.

В этом пути нет готового пакета MT6897/degas и нет общего кода, который автоматически принимает штатный DTB и превращает его в карту памяти платформы. Входные регистры загрузчика также не сохраняются как контракт для последующих стадий. Поэтому DTB, live Android и разбор LK нужны как источники фактов для нового статического описания и, при необходимости, нового механизма передачи DTB, а не как данные, которые текущий SEC уже умеет использовать.

## Точные точки интеграции

| Узел | Файл в зафиксированной ревизии | Что делает код | Что должно быть доказано для degas |
|---|---|---|---|
| Упаковка и перенос FD | [`BootShim/BootShim.S`](https://github.com/Project-Silicium/Mu-Silicium/blob/964a886f3d1ad3f35137435b11287c8d6827437b/BootShim/BootShim.S), [`build_uefi.py`](https://github.com/Project-Silicium/Mu-Silicium/blob/964a886f3d1ad3f35137435b11287c8d6827437b/build_uefi.py) | Shim подставляет адрес `_Payload` в `x1`, копирует ровно `FD_SIZE` байт в `FD_BASE` блоками по 16 и переходит на `FD_BASE`. Сборочный скрипт может добавить shim, сжать FD, добавить DTB и упаковать kernel через `mkbootimg.py`. | Фактический формат boot/vendor_boot, допустимый kernel payload, сжатие, необходимость ARM64 kernel header, адрес загрузки исходного payload, целевой `FD_BASE`, максимальный безопасный `FD_SIZE`, точка входа и поведение загрузчика при appended DTB. Диапазоны источника и назначения должны быть доступны и не иметь опасного перекрытия. Размер FD должен соответствовать циклу копирования и FDF. |
| Первый код Mu | [`Sec/AArch64/ModuleEntryPoint.S`](https://github.com/Project-Silicium/Mu-Silicium/blob/964a886f3d1ad3f35137435b11287c8d6827437b/Silicon/Silicium/SiliciumPkg/Sec/AArch64/ModuleEntryPoint.S) | Вызывает платформенный assembler hook, настраивает EL1/EL2, VFP и векторы, берёт base/size стека из PCD, очищает стек и вызывает `SecEntry`. После цикла очистки `x3` уже равен концу диапазона, но именно он передаётся в `x0` как `StackBase`; это требует отдельной проверки/исправления до аппаратного запуска. | Текущий EL (поддерживаемый путь EL1 или EL2), состояние MMU/cache/interrupts, доступность архитектурных регистров, допустимый и выровненный диапазон стека. Нужен снимок входных `x0…x3` отдельным диагностическим payload, если они могут нести DTB/handoff: этот entry их не сохраняет. Статически проверить адреса cache invalidation и Stack HOB с учётом замечания о `x3`. |
| SEC и передача в DXE | [`Sec/Sec.c`](https://github.com/Project-Silicium/Mu-Silicium/blob/964a886f3d1ad3f35137435b11287c8d6827437b/Silicon/Silicium/SiliciumPkg/Sec/Sec.c), [`Sec/Sec.inf`](https://github.com/Project-Silicium/Mu-Silicium/blob/964a886f3d1ad3f35137435b11287c8d6827437b/Silicon/Silicium/SiliciumPkg/Sec/Sec.inf) | Отключает D-cache, инвалидирует I-cache, стек и FD, ставит векторы; затем ищет `DXE Heap`, создаёт HOB list, строит memory/stack/CPU HOB, распаковывает FV и загружает DXE Core. | Точные непересекающиеся области `UEFI Stack`, `UEFI FD`, `DXE Heap`; доступность FD после возможного перемещения; требуемые cache maintenance действия с учётом состояния, оставленного LK; физические address bits; безопасный ранний канал наблюдения. |
| Платформенный ранний hook | [`MT6789 PlatformHelper.S`](https://github.com/Project-Silicium/Mu-Silicium/blob/964a886f3d1ad3f35137435b11287c8d6827437b/Silicon/MediaTek/MT6789Pkg/Library/PlatformSecLib/AArch64/PlatformHelper.S), [`PlatformSec.c`](https://github.com/Project-Silicium/Mu-Silicium/blob/964a886f3d1ad3f35137435b11287c8d6827437b/Silicon/MediaTek/MT6789Pkg/Library/PlatformSecLib/PlatformSec.c), [`PlatformRegisters.h`](https://github.com/Project-Silicium/Mu-Silicium/blob/964a886f3d1ad3f35137435b11287c8d6827437b/Silicon/MediaTek/MT6789Pkg/Library/PlatformSecLib/PlatformRegisters.h) | Ассемблерный hook MT6789 пуст. C hook ищет регион `WatchDog Timer` и записывает донорский ключ в его base. | Для MT6897 отдельно доказать необходимость обслуживания watchdog, base, offset, ширину и семантику записи. До этого degas-реализация должна избегать донорской MMIO-записи. Также установить, нужна ли ранняя инициализация coherency/GIC/system counter. |
| Первичная карта и MMU | [`MemoryInitPei.c`](https://github.com/Project-Silicium/Mu-Silicium/blob/964a886f3d1ad3f35137435b11287c8d6827437b/Silicon/Silicium/SiliciumPkg/Library/MemoryInitPeiLib/MemoryInitPei.c), [`MemoryMapHelper.c`](https://github.com/Project-Silicium/Mu-Silicium/blob/964a886f3d1ad3f35137435b11287c8d6827437b/Silicon/Silicium/SiliciumPkg/Library/MemoryMapHelperLib/MemoryMapHelper.c) | Получает массив из платформенной `MemoryMapLib`, создаёт resource/allocation HOB, строит identity map MMU. Регионы затем ищутся по строковым именам. Проверка пересечений пропускает адреса ниже `PcdSystemMemoryBase`. | Полная физическая карта: usable RAM, secure/firmware/modem/DSP/GPU/display/ramoops и другие carveout, FD/heap/stack, нужные MMIO. Для каждого региона нужны base, length, назначение, cacheability, resource type, EFI memory type и решение о HOB. Проверить переполнения и пересечения внешним валидатором, включая MMIO ниже RAM base. |
| Определение объёма RAM | [`MediaTek RamManagerLib`](https://github.com/Project-Silicium/Mu-Silicium/blob/964a886f3d1ad3f35137435b11287c8d6827437b/Silicon/MediaTek/MediaTekPkg/Library/RamManagerLib/RamManager.c), [`RamManagerDxe`](https://github.com/Project-Silicium/Mu-Silicium/blob/964a886f3d1ad3f35137435b11287c8d6827437b/Silicon/Silicium/SiliciumPkg/Drivers/RamManagerDxe/RamManager.c) | MediaTek-библиотека читает `EMI_CONA`/`EMI_CONH`, вычисляет общий размер и возвращает один сплошной range от `PcdSystemMemoryBase`; DXE отмечает незанятые части этого range как conventional memory. | Подтвердить по исходникам именно MT6897 и независимым наблюдением: EMI base, offsets/bitfields, число channel/rank, формулу размера и RAM base. Проверить результат для каждого варианта RAM устройства. Утверждение кода «MediaTek doesn't have memory holes» нельзя переносить: carveout и недоступные интервалы должны быть исключены либо представлены уже занятыми до `RamManagerDxe`. |
| Ранний текст в framebuffer | [`FrameBufferSerialPortLib.c`](https://github.com/Project-Silicium/Mu-Silicium/blob/964a886f3d1ad3f35137435b11287c8d6827437b/Silicon/Silicium/SiliciumPkg/Library/FrameBufferSerialPortLib/FrameBufferSerialPortLib.c), [`FrameBuffer.h`](https://github.com/Project-Silicium/Mu-Silicium/blob/964a886f3d1ad3f35137435b11287c8d6827437b/Silicon/Silicium/SiliciumPkg/Library/FrameBufferSerialPortLib/FrameBuffer.h) | Ищет регион `Display Reserved`, считает pitch равным `width × bytes_per_pixel`, хранит позицию курсора в последних 8 байтах региона и flush-ит весь range. В DEBUG SEC сначала обнуляет весь display reserved range. | Base и фактическая длина буфера, active width/height, bytes/bits per pixel, byte order, pitch, rotation, double/triple buffering, выбранный scanout buffer, cache/coherency. Дисплейный тракт должен оставаться включённым после LK. Нужен безопасный тест записи небольшой метки; нельзя очищать весь carveout до проверки, что там нет иных структур и secure content. |
| GOP поверх готового scanout | [`SimpleFbDxe/SimpleFb.c`](https://github.com/Project-Silicium/Mu-Silicium/blob/964a886f3d1ad3f35137435b11287c8d6827437b/Silicon/Silicium/SiliciumPkg/Drivers/SimpleFbDxe/SimpleFb.c) | Публикует один GOP mode. `PixelsPerScanLine = width`, `FrameBufferSize = width × height × depth/8`; поддерживает 16/24/32 bpp, а для 32 bpp жёстко выбирает BGR-reserved. Контроллер дисплея не инициализирует. | Все параметры предыдущего пункта и совпадение реального pitch/format с ограничениями драйвера. Если pitch отличается или формат иной, драйвер нужно расширять. Это переиспользование уже работающего framebuffer, а не драйвер DSI/панели/GPU. |
| SoC PCD и общие драйверы | [`MT6789Pkg.dsc.inc`](https://github.com/Project-Silicium/Mu-Silicium/blob/964a886f3d1ad3f35137435b11287c8d6827437b/Silicon/MediaTek/MT6789Pkg/MT6789Pkg.dsc.inc), [`MediaTekPkg.dsc.inc`](https://github.com/Project-Silicium/Mu-Silicium/blob/964a886f3d1ad3f35137435b11287c8d6827437b/Silicon/MediaTek/MediaTekPkg/MediaTekPkg.dsc.inc), [`SiliciumPkg.dsc.inc`](https://github.com/Project-Silicium/Mu-Silicium/blob/964a886f3d1ad3f35137435b11287c8d6827437b/Silicon/Silicium/SiliciumPkg/SiliciumPkg.dsc.inc) | SoC include фиксирует ISA flags, core/cluster count, GIC и тип generic timer. Общий MediaTek include подключает GPIO, clocks, PMIC wrapper/PMIC и MSDC. | Для MT6897 получить topology/ISA из CPU/kernel, GIC version/base/size/interrupts из DT и исходников, timer access/frequency из регистров и DT, PSCI conduit/functions из DT/SMCCC. Каждый общий MediaTek driver включать только после сверки compatible/IP revision, clocks/resets/power domains, MMIO и interrupt map; иначе убрать из первой конфигурации. |

## Проверенная расшифровка `atag,videolfb`

В открытой ветке MiCode [`bsp-degas-u-oss`](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/tree/bsp-degas-u-oss) опубликовано ядро [6.1.57](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/bsp-degas-u-oss/Makefile), но проверенное дерево содержит общий DRM MediaTek и не содержит каталога `drivers/misc/mediatek` с vendor consumer этого тега. Поэтому точный исходник потребителя MT6897 в этой публикации не установлен.

Первичный открытый источник MediaTek показывает исторический ABI без предположений о графическом формате: [`drivers/misc/mediatek/video/common/mtkfb.c`](https://android.googlesource.com/kernel/mediatek/+/4f43e6b499c6d194030df8d2506485db9d5165bd/drivers/misc/mediatek/video/common/mtkfb.c) получает `atag,videolfb` через `of_get_flat_dt_prop()` и напрямую приводит указатель к следующей структуре:

```c
struct tag_videolfb {
    u64 fb_base;
    u32 islcmfound;
    u32 fps;
    u32 vram;
    char lcmname[1];
};
```

Код читает эти поля как native little endian и использует `fb_base`/`vram` для резервного диапазона framebuffer. Он не получает из тега width, height, pitch, depth или pixel format.

**Проверено на live blob degas.** В [`live.dts`](../evidence/raw/2026-09-14-initial/live.dts) свойство имеет 52 байта. Если трактовать сырые байты согласно структуре выше, а не как числовые DTS cells, получается:

| Offset | Сырые байты | Поле | Результат |
|---:|---|---|---|
| 0 | `00 f0 c2 fc 00 00 00 00` | `u64 fb_base` | `0x00000000fcc2f000` |
| 8 | `01 00 00 00` | `u32 islcmfound` | `1` |
| 12 | `06 2f 00 00` | `u32 fps` | `0x2f06` = `12038` в единицах тега |
| 16 | `00 00 3d 03` | `u32 vram` | `0x033d0000` = `54 329 344` байт |
| 20 | ASCII + NUL/padding | `lcmname` | `n12a_36_02_0b_dsc_cmd_lcm_drv` |

Это декодирование независимо совпадает с отдельными свойствами `atag,videolfb-fb_base_l = 0xfcc2f000`, `atag,videolfb-fb_base_h = 0`, `atag,videolfb-vramSize = 0x033d0000`, с `reg` узла `/reserved-memory/mblock-15-framebuffer` и с inclusive-диапазоном `fcc2f000-ffffefff` в [`iomem.txt`](../evidence/raw/2026-09-14-initial/iomem.txt). Проверенный вывод: зарезервированный диапазон равен `[0xfcc2f000, 0xfffff000)`. Свойства подтверждают адрес и размер резерва; они не подтверждают, что его начало является текущим scanout buffer.

**Что из этих тегов вывести нельзя.** Width/height, pitch, число буферов, активное смещение, bpp и channel order отсутствуют и в показанной структуре, и в auxiliary properties. `vram / (width × height)` не определяет bpp: резерв может включать несколько буферов, alignment и служебные области. Имя `n12a_36_02_0b_dsc_cmd_lcm_drv` помогает найти panel/LCM implementation, но само по себе не задаёт layout пикселей. До получения runtime plane/framebuffer state или соответствующего LK/display source нельзя выставлять `PcdFrameBufferWidth`, `PcdFrameBufferHeight`, `PcdFrameBufferColorDepth` и нельзя предполагать 32 bpp.

## Дополнение: точный установленный consumer и требования к pitch

После первого прохода извлечён `mediatek-drm.ko` из активного `vendor_boot_b` (SHA-256 `4ab3c4f70930259695c31bf41b31eb3f14cfaeeb1a4e42a8e8a95b67a63ff356`). Его ELF build ID побайтово совпал с `/sys/module/mediatek_drm/notes/.note.gnu.build-id` работающего телефона. Аналогично проверен модуль выбранной панели. Это подтверждает идентичность сборки; не является сравнением всех байтов загруженного кода.

Точная дизассемблировка по границам ELF-символов сохранена в [boot-fb-exact.asm](../evidence/extracted/2026-09-14/display-analysis/boot-fb-exact.asm). `_parse_tag_videolfb` (`.text+0x2754`, 440 байт) читает `/chosen/atag,videolfb`: base как native little-endian u64 с offset 0, fps как u32 с offset 12 и размер u32 с offset 16. Таким образом, декодирование base и размера выше теперь подтверждено установленным consumer, а не только историческим ABI.

`mtk_crtc_fill_fb_para` (`.text+0x5f408`, 240 байт) округляет ширину и высоту до кратных 32; pitch равен округлённой ширине × 4, высота allocation равна округлённой высоте × 3. Для режима 1220 × 2712 это 1248 пикселей / 4992 байта в строке и 8160 строк allocation. Это контракт импорта boot framebuffer ядром, а не самостоятельное измерение активного scanout LK.

`mtk_drm_fb_gem_insert` (`.text+0xb5fd4`, 1212 байт) строит страницы из физического адреса, CPU mapping через `vmap` и DMA mapping через `dma_map_sg_attrs`. Поэтому адреса Android DRM/DMA нельзя подставлять вместо CPU physical address. Наличие `free_fb_buf` с `__free_pages` также запрещает считать старый boot framebuffer постоянно принадлежащим LK в работающем Android; сам факт наличия функции не доказывает её вызов в этом запуске.

Окончательная сверка LK и ограничения первого вывода изложены в [FRAMEBUFFER-CONTRACT.md](FRAMEBUFFER-CONTRACT.md), карта резервов — в [MEMORY-RECONCILIATION.md](MEMORY-RECONCILIATION.md). Ограничения штатных Mu FrameBufferSerialPortLib / SimpleFbDxe на pitch из таблицы выше необходимо устранить до использования режима 1220 × 2712 с pitch 4992.

## Минимальный комплект доказательств M1

Каждая запись должна иметь значение, первичный источник, файл/команду извлечения, SHA-256 входного артефакта и статус `наблюдалось / выведено / не подтверждено`.

1. **Контейнер и handoff:** распакованные заголовки `boot`, `init_boot`, `vendor_boot`; cmdline/bootconfig; kernel compression; DTB/DTBO composition; дизассемблированный участок LK, который загружает kernel и формирует регистры; live-снимок `CurrentEL`, `SCTLR_ELx`, `TCR_ELx`, `TTBRx_ELx`, `DAIF`, `x0…x3` из отдельного минимального payload. Android boot header описывает контейнер, но не доказывает физический адрес исполнения.
2. **Карта RAM:** DT `/memory`; `/reserved-memory` со всеми `reg`, `no-map`, `reusable`, `shared-dma-pool`; `/proc/iomem`; `/sys/firmware/devicetree/base`; ION/dma-heap/DRM/reserved-memory сообщения kernel; LK/bootloader memory tags. Расхождения сохранять, не объединять автоматически.
3. **Размещение Mu:** выбранные `FD_BASE`, `FD_SIZE`, stack и DXE heap с доказательством свободного диапазона на всём времени работы firmware. Проверить отсутствие пересечения с загруженным kernel payload, ramdisk, DTB, decompressor workspace, framebuffer, secure firmware и всеми carveout. Нужен расчёт границ `[base, base + size)` и выравнивания.
4. **Framebuffer:** live `dumpsys SurfaceFlinger`, DRM/fb sysfs и kernel logs; DT reserved-memory/display nodes; LK display code/strings; если доступно, параметры scanout из debugfs. Зафиксировать base, buffer length, width, height, pitch, fourcc/channel order, bpp, rotation, номер активного буфера, cache policy и факт сохранения изображения при переходе из LK.
5. **Ранние блоки:** watchdog, GIC, architectural timer/system counter, PSCI/SMCCC и возможный UART. Для каждого: compatible/IP revision, base/size, interrupts, clocks/resets/power-domain, security ownership и разрешённость доступа из текущего EL. Значение из другого SoC остаётся только гипотезой.
6. **Остальное оборудование:** UFS/eMMC, USB, GPIO/кнопки, PMIC/regulators, clocks, display/DSI/panel/backlight, touch, I2C/SPI, audio, Wi-Fi/Bluetooth, GPU, modem, sensors, thermal и battery/charging оформить той же схемой. На M1 достаточно инвентаря из DT + live enumeration + kernel driver binding; MMIO-записи и перенос драйверов откладываются до проверки конкретного IP.

## Ворота перед созданием `degasPkg`

- **G1 — вход:** воспроизводимо известны формат payload, точка входа, EL и доступный ранний сигнал.
- **G2 — размещение:** FD, stack и heap имеют подтверждённые интервалы; автоматическая проверка не находит пересечений со всеми известными reserved ranges.
- **G3 — карта:** для каждой записи будущей `MemoryMapLib.c` указан источник и атрибут; неизвестные интервалы не объявляются conventional memory.
- **G4 — безопасный SEC:** watchdog и иные MMIO-записи отсутствуют, пока адрес, offset и семантика MT6897 не подтверждены.
- **G5 — экран:** либо подтверждён полный simple-framebuffer contract, либо ранний DEBUG использует другой канал. Одних width/height недостаточно.
- **G6 — RAM manager:** донорская EMI-формула включается только после сверки с MT6897 и независимой проверки результата. До этого usable ranges задаются консервативно из подтверждённой карты.

## Как использовать донорские пакеты

[`lancelotPkg/MemoryMapLib.c`](https://github.com/Project-Silicium/Mu-Silicium/blob/964a886f3d1ad3f35137435b11287c8d6827437b/Platforms/Xiaomi/lancelotPkg/Library/MemoryMapLib/MemoryMapLib.c), [`lancelot.dsc`](https://github.com/Project-Silicium/Mu-Silicium/blob/964a886f3d1ad3f35137435b11287c8d6827437b/Platforms/Xiaomi/lancelotPkg/lancelot.dsc) и [`lancelot.fdf`](https://github.com/Project-Silicium/Mu-Silicium/blob/964a886f3d1ad3f35137435b11287c8d6827437b/Platforms/Xiaomi/lancelotPkg/lancelot.fdf) полезны как пример структуры MediaTek-платформы: именованные регионы, PCD, выбор `MemoryMapLib`, включение SEC и `RamManagerDxe`. Все числовые значения, перечень carveout, framebuffer geometry и набор драйверов относятся к другой плате/SoC и не являются начальными значениями для degas.

Первый `degasPkg` следует делать минимальным: SEC, подтверждённая карта, таймер и один проверенный канал диагностики. GPIO, PMIC, storage и полный display path добавляются по одному после получения соответствующей строки доказательств.
