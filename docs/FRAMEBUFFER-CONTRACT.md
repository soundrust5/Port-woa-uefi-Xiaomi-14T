# Контракт framebuffer для degas: собранные данные и границы доказательства

Снимок 2026-09-14. Цель — параметры раннего вывода Mu-Silicium после штатного LK. Это исследование штатной загрузки; собственный payload ещё не запускался.

## Сводка параметров

| Параметр | Значение | Доказательство и область применимости |
|---|---|---|
| CPU physical base резерва | `0xFCC2F000` | live DT: основной/auxiliary videolfb, reserved-memory; журнал LK allocation и disp_init; iomem/memblock |
| Длина резерва | `0x033D0000` = 54 329 344 байта | Те же независимые источники |
| Конец резерва, не включительно | `0xFFFFF000` | base + size; последняя страница ниже 4 GiB не входит в framebuffer |
| LK virtual mapping | `0xFFFF000000200000` | `disp_init` в PL/LK log; адрес имеет смысл только при соответствующих таблицах трансляции LK |
| Выбранная панель | `n12a_36_02_0b_dsc_cmd_lcm_drv` | videolfb, LK log и debug state установленного DRM |
| Видимая геометрия | 1220 × 2712 | LK отрисовка до x=1219/y=2711 и DRM mode/debug state |
| Размер пикселя основного слоя | 4 байта / 32 bpp | Точный BL2_EXT getter и основной LK logo writer |
| Offset основного слоя | `0` от physical base | BL2_EXT disp_init настраивает layer 0 на raw framebuffer PA; main LK рисует через mt_get_fb_addr() |
| Порядок байтов основного слоя | B, G, R, A | Format immediate `0x01002002`; main LK пишет AARRGGBB word без перестановки на little endian |
| GOP формат | `PixelBlueGreenRedReserved8BitPerColor` | RGB-каналы совпадают с доказанным layout; верхний байт в LK alpha, поэтому для непрозрачной метки задавать `0xFF` |
| Pitch основного слоя LK | 4992 байта / 1248 пикселей | BL2_EXT: align(width,32) << 2; main LK: needAlign=1 и row*alignedWidth |
| Размер одного выровненного кадра | `0x00CF3000` = 13 578 240 байт | 1248 × 2720 × 4; ровно 4 таких кадра + `0x4000` составляют весь reserve, но это арифметика, не доказательство четырёх активных кадров |
| Отдельный слой boot menu | base `0xFD922000`, pitch 4880 | BL2_EXT layer 1: base + frame size; не параметры основного слоя |
| Pitch импорта boot framebuffer ядром | 4992 байта / 1248 пикселей | Точный установленный `mtk_crtc_fill_fb_para`: align(width,32) × 4 |
| Высота allocation при импорте ядром | 8160 строк | align(2712,32) × 3; это не видимая высота и не доказательство трёх активных scanout |
| Поле fps в теге | 12038, raw units | LE u32 offset 12; не подменять им измеренную частоту/тайминги панели |
| Android DRM/DMA addresses | Например `0x390000000` | Отдельное адресное пространство DMA/IOVA; не CPU physical base для Mu |

Полный reserve не равен одному кадру. Нельзя вычислять bpp или количество кадров делением его размера на геометрию. Адрес LK virtual mapping нельзя использовать в Mu без воспроизведения соответствующей трансляции. Текущий Android scanout не описывает автоматически состояние в момент передачи управления от LK.

## Проверка установленного кода

Из активного `vendor_boot_b` извлечены DRM и panel ELF-модули. Секции `.note.gnu.build-id` обоих модулей побайтово совпали со снятыми через sysfs заметками загруженных модулей. SHA-256 DRM ELF: `4ab3c4f70930259695c31bf41b31eb3f14cfaeeb1a4e42a8e8a95b67a63ff356`.

[Точная дизассемблировка](../evidence/extracted/2026-09-14/display-analysis/boot-fb-exact.asm) доказывает чтение videolfb как native little endian, формулу pitch/размера и отдельное DMA mapping через `dma_map_sg_attrs`. Для получения полного тела функций использованы границы ELF symbol size, а не только остановка disassembler на ближайшей метке.

Живые файлы, команды чтения и SHA-256: [manifest](../evidence/raw/2026-09-14-memory-display/manifest.json). Журнал загрузчика: [pl_lk.txt](../evidence/raw/2026-09-14-memory-display/pl_lk.txt). Разбор точного LK: [LK-FRAMEBUFFER-ANALYSIS.md](LK-FRAMEBUFFER-ANALYSIS.md). Автоматическая сверка независимых полей/образов/build IDs: [результат](../evidence/validation-memory-display-2026-09-14.json), команда `python tools/verify_display_evidence.py`.

## Что это меняет в Mu

В проверенной ревизии Mu `FrameBufferSerialPortLib` и `SimpleFbDxe` выводят pitch из видимой ширины. При width=1220 и 32 bpp получается 4880 вместо подтверждённых LK и kernel handoff 4992 байт. Нужно независимое поле stride: видимая width остаётся 1220, а `PixelsPerScanLine` для pitch=4992 должен быть 1248. Подмена видимой ширины на 1248 скрывает ошибку интерфейса.

Для основного слоя известны offset=0 и выровненный кадр `0x00CF3000`; интерфейс должен отдельно задавать длину поверхности и её смещение. Нельзя обнулять все 54 MB резерва или размещать служебные переменные в его хвосте только потому, что так делает донорский DEBUG-код. Первое изображение — ограниченная метка в проверенной поверхности с уникальным build ID.

## Что требует проверки при реальном входе

Даже верные base/pitch/format не гарантируют обновления панели. При входе нашего payload нужно проверить сохранность настроенного основного слоя и display pipeline, режим DSI и необходимость trigger/TE/command queue, cache maintenance и доступность памяти в фактическом translation regime. Имя панели с `cmd` и неподвижный логотип не доказывают, что запись в RAM сразу станет видимой.

Первый аппаратный опыт должен различать «payload не исполнялся» и «payload исполнялся, но канал вывода не обновился». Поэтому перед полноценным Mu нужен минимальный диагностический вход с сохранением доступного контекста и обоснованным способом прочитать свежую метку. Нет результата такого опыта — нет утверждения о работающем framebuffer.

Карта памяти, временные allocations LK и ограничения выбора FD/stack/heap: [MEMORY-RECONCILIATION.md](MEMORY-RECONCILIATION.md). Адреса этих областей не назначаются по одному лишь списку свободной памяти Android.
