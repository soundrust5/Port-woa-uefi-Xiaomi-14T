# Сверка DRAM и резервов Xiaomi 14T (`degas`)

Дата снимка: 2026-09-14. Границы диапазонов ниже полуоткрытые: `[base, end)`.

Это аудит фактов перед реализацией Mu, а не готовая `MemoryMapLib`. Он сводит живой FDT, `/proc/iomem`, позднее состояние Linux `memblock`, конфигурацию ядра и журнал PL/LK. Ни один диапазон для FD, стека или heap здесь не объявлен безопасным.

## Итог

Живой FDT объявляет один DRAM-диапазон `[0x40000000, 0x340000000)` размером `0x300000000` (12 GiB), 52 статических `reserved-memory` диапазона с `reg` и четыре узла, размещаемых аллокатором по `size`/`alignment`/`alloc-ranges`. Блок FDT memory reservation map пуст; резервы находятся в дереве, а не в FDT header reserve map.

52 статических диапазона не пересекаются. Их объединение занимает `0x2e8c4000` (744.765625 MiB) и распадается на 29 смежных групп. Из них 48 имеют `no-map`; все 48 исключены из верхнеуровневых строк `System RAM` в `/proc/iomem`. Четыре без `no-map` — `aee_lk`, `framebuffer`, `gz-log`, `log_store` — находятся внутри `System RAM`, но отмечены дочерними строками `reserved`. Отсутствие `no-map` не означает право записи.

Четыре динамических узла имеют по одному точному адресу, совместимому с их ограничениями, в снятом `/proc/iomem`. Для SSMR имя, base и size напрямую подтверждены debugfs CMA и точной строкой `memblock.reserved`; две области DPMAIF объединены там в одну точную строку. Общий union статических и этих четырёх runtime-размещений равен `0x36ab4000` (874.703125 MiB), пересечений нет.

Вендорский `/memory/mblock_info` декодируется в 29 свободных интервалов. Их union `0x2d173c000` и они **в точности** равны дополнению 52 статических `reg` диапазонов во всех 12 GiB. Это сильная внутренняя проверка формата и одновременно доказательство ограничения: этот список ещё содержит адреса ядра, DTB, initrd, динамических CMA/DPMAIF резервов и Linux runtime allocations. Он не является списком памяти, безопасной для Mu.

## Источники и степень доверия

- `evidence/extracted/2026-09-14/live-fdt.json`: разобранный живой FDT, SHA-256 `0bfe978cc3792e0734681f9b3cac168ccfb600fb4e3cb87ebcf8091d5282b11a`.
- `evidence/raw/2026-09-14-initial/iomem.txt`: иерархия физических ресурсов Linux. Она округляет и объединяет часть записей.
- `evidence/raw/2026-09-14-memory-display/memblock-memory.txt` и `memblock-reserved.txt`: позднее живое состояние раннего аллокатора Linux. `reserved` содержит 123 точные, в том числе субстраничные, записи.
- `evidence/raw/2026-09-14-memory-display/pl_lk.txt`: реальный кольцевой журнал PL/LK. Он содержит точные `mblock_alloc_range`, `mblock-R` и `mblock_free_with_size`, но заканчивается посреди следующего прохода и местами содержит повторённые/перемежённые события.
- `evidence/raw/2026-09-14-initial/meminfo.txt`, `evidence/raw/2026-09-14-memory-display/kernel-memory-config.txt` и файлы `cma-*.txt`: `CmaTotal=147456 kB`, параметры ядра и точные PFN/size/usage двух CMA-областей.
- Заголовки `live-b-boot` и `live-b-vendor_boot` в `evidence/extracted/2026-09-14/boot-images-complete/`. Снимок `bootconfig.txt` задаёт активный slot `_b`. Заголовки `boot_a` и `boot_b` побайтово одинаковы; адреса загрузки ниже всё равно взяты из активного `live-b-vendor_boot`.
- Машиночитаемый полный результат: `evidence/extracted/2026-09-14/memory-reconciliation.json`.

Декодирование начала `mblock_info` использует native little-endian layout: `u32 count`, четыре байта выравнивания, затем 24-байтные записи `u64 start`, `u64 size`, `rank` в восьмибайтном слоте. Оно помечено как вывод, а не как найденный binding. Вывод самопроверяется точным равенством 29 декодированных интервалов дополнению всех 52 carveout. Из 22552 байт свойства этим layout объяснены первые 704; хвост 21848 байт сохранён raw в `live-fdt.json` и намеренно не интерпретирован без binding. Публичный исходный код MediaTek LK показывает аналогичный механизм: kernel/ramdisk/DTB резервируются, а затем возвращаются через `mblock_create` перед handoff: [LK `mt_boot.c`, commit `37ec8bc`](https://github.com/svoboda18/lk/blob/37ec8bcbc4361c0e267617f517d154656804c17a/app/mt_boot/mt_boot.c). Это подтверждает интерпретацию механизма, но не доказывает, что снятый бинарный LK собран именно из этого исходника.

## Все 52 статических carveout

`NM` означает свойство DT `no-map`. `LK exact` означает, что тот же base/size непосредственно присутствует в доступной части `pl_lk` как allocation или `mblock-R`. Для остальных 25 записей журнал оборван раньше соответствующей части; их подтверждают живой FDT, точное дополнение `mblock_info` и `/proc/iomem`, но не отдельная строка LK.

| Узел | Base | End | Size | DT | Linux | LK |
|---|---:|---:|---:|---|---|---|
| `mblock-52-sspm_ap-shared` | `0x9f06f000` | `0x9f17f000` | `0x110000` | NM | top reserved | — |
| `mblock-51-platform-debug_slbc_shared` | `0xfcbef000` | `0xfcc2f000` | `0x40000` | NM | top reserved | — |
| `mblock-50-me_GPUEB_SHARED` | `0x7fe70000` | `0x7fff0000` | `0x180000` | NM | top reserved | — |
| `mblock-49-me_CSFFWLOG_reserved` | `0x7fffb000` | `0x7ffff000` | `0x4000` | NM | top reserved | — |
| `mblock-48-adsp_shared_reserved` | `0xf8000000` | `0xf8810000` | `0x810000` | NM | top reserved | — |
| `mblock-47-debug_met_res-ram` | `0x9f17f000` | `0x9fd7f000` | `0xc00000` | NM | top reserved | — |
| `mblock-46-scp_emi_reserved` | `0x89f00000` | `0x89f20000` | `0x20000` | NM | top reserved | — |
| `mblock-45-shared-dma-pool_wifi-reserve-memory_dma` | `0x88b00000` | `0x89f00000` | `0x1400000` | NM | top reserved | — |
| `mblock-44-consys_emi_reserved` | `0x87e00000` | `0x88b00000` | `0xd00000` | NM | top reserved | — |
| `mblock-43-me_GPUmputab_PMA` | `0x7e800000` | `0x7ee00000` | `0x600000` | NM | top reserved | — |
| `mblock-42-me_vcp_reserved` | `0xbf500000` | `0xbfd00000` | `0x800000` | NM | top reserved | — |
| `mblock-41-adsp_image_reserved` | `0x7a000000` | `0x7b530000` | `0x1530000` | NM | top reserved | — |
| `mblock-40-md_mem_usage` | `0xed780000` | `0xee000000` | `0x880000` | NM | top reserved | — |
| `mblock-39-md_mem_usage` | `0xd4000000` | `0xdb3c0000` | `0x73c0000` | NM | top reserved | — |
| `mblock-38-md_mem_usage` | `0xd3400000` | `0xd3650000` | `0x250000` | NM | top reserved | — |
| `mblock-37-ap_md_c_smem` | `0x8a000000` | `0x8c560000` | `0x2560000` | NM | top reserved | — |
| `mblock-36-md_mem_usage` | `0xd0000000` | `0xd33c0000` | `0x33c0000` | NM | top reserved | — |
| `mblock-35-ccci_tag_mem` | `0xbfdf0000` | `0xbfe00000` | `0x10000` | NM | top reserved | — |
| `mblock-34-apu_sapu_apurvgzctrlshm` | `0x8f900000` | `0x8fa00000` | `0x100000` | NM | top reserved | — |
| `mblock-33-apu_apusys-rv_aee-coredump` | `0x8ed50000` | `0x8f000000` | `0x2b0000` | NM | top reserved | — |
| `mblock-32-apu_apusys-rv_secure` | `0x8fa00000` | `0x90000000` | `0x600000` | NM | top reserved | — |
| `mblock-31-mm_ccu_reserved` | `0x80000000` | `0x80100000` | `0x100000` | NM | top reserved | — |
| `mblock-30-reserve-memory-scp_share` | `0x8f000000` | `0x8f898000` | `0x898000` | NM | top reserved | — |
| `mblock-29-SCP-reserved` | `0xb8000000` | `0xb9f00000` | `0x1f00000` | NM | top reserved | — |
| `mblock-28-ap_md_nc_smem` | `0x8e000000` | `0x8e130000` | `0x130000` | NM | top reserved | — |
| `mblock-27-platform-debug_dfdsoc_dump` | `0xfc400000` | `0xfc700000` | `0x300000` | NM | top reserved | exact |
| `mblock-26-tee-shm` | `0x7f000000` | `0x7f400000` | `0x400000` | NM | top reserved | exact |
| `mblock-25-tee-reserved` | `0xc0000000` | `0xce000000` | `0xe000000` | NM | top reserved | exact |
| `mblock-24-tee-secmem` | `0x7f400000` | `0x7fd00000` | `0x900000` | NM | top reserved | exact |
| `mblock-23-MCUPM-reserved` | `0x7fd00000` | `0x7fe00000` | `0x100000` | NM | top reserved | exact |
| `mblock-22-SSPM-reserved` | `0x9fd7f000` | `0x9ffff000` | `0x280000` | NM | top reserved | exact |
| `mblock-21-platform_mtksmmu_protpgd` | `0x7be00000` | `0x7e000000` | `0x2200000` | NM | top reserved | exact |
| `mblock-20-gz-log` | `0x33d600000` | `0x33d641000` | `0x41000` | mapped | RAM + child reserved | exact |
| `mblock-19-unmap2` | `0x33d800000` | `0x33da00000` | `0x200000` | NM | top reserved | exact |
| `mblock-18-mkp` | `0x33da00000` | `0x33dc00000` | `0x200000` | NM | top reserved | exact |
| `mblock-17-gz` | `0x33dc00000` | `0x33fe00000` | `0x2200000` | NM | top reserved | exact |
| `mblock-16-me_cmdq_reserved` | `0x33fe00000` | `0x33ff00000` | `0x100000` | NM | top reserved | exact |
| `mblock-15-framebuffer` | `0xfcc2f000` | `0xfffff000` | `0x33d0000` | mapped | RAM + child reserved | exact |
| `mblock-14-me_gpu_reserved` | `0x7ee00000` | `0x7f000000` | `0x200000` | NM | top reserved | exact |
| `mblock-13-security_dice_common` | `0x6fffe000` | `0x6ffff000` | `0x1000` | NM | top reserved | exact |
| `mblock-12-atf-log-reserved` | `0xbfe00000` | `0xc0000000` | `0x200000` | NM | top reserved | exact |
| `mblock-11-log_store` | `0x33ffbe000` | `0x33fffe000` | `0x40000` | mapped | RAM + child reserved | exact |
| `mblock-10-emi_mbist_buf` | `0x7ffff000` | `0x80000000` | `0x1000` | NM | top reserved | exact |
| `mblock-9-dramc-rk1` | `0x33fffe000` | `0x340000000` | `0x2000` | NM | top reserved | exact |
| `mblock-8-dramc-rk0` | `0x1bfffe000` | `0x1c0000000` | `0x2000` | NM | top reserved | exact |
| `mblock-7-platform-debug_dfdmcu_dump` | `0xfc800000` | `0xfca80000` | `0x280000` | NM | top reserved | exact |
| `mblock-6-crypto_hw` | `0x48401000` | `0x48402000` | `0x1000` | NM | top reserved | exact |
| `mblock-5-aee_lk` | `0x7e000000` | `0x7e800000` | `0x800000` | mapped | RAM + child reserved | exact |
| `mblock-4-BL31-reserved` | `0x48800000` | `0x48a00000` | `0x200000` | NM | top reserved | exact |
| `mblock-3-minirdump` | `0x48170000` | `0x48180000` | `0x10000` | NM | top reserved | exact |
| `mblock-2-pstore` | `0x48090000` | `0x48170000` | `0xe0000` | NM | top reserved | exact |
| `mblock-1-aee_debug_kinfo` | `0x48080000` | `0x48090000` | `0x10000` | NM | top reserved | exact |

Имена и `compatible` дают домен владельца, но сами по себе не доказывают, что CPU может читать регион или что firmware image занимает весь carveout. К защищённым доменам относятся BL31/ATF, TEE, GZ/MKP, SCP/SSPM/MCUPM, modem/CCCI, ADSP, APU, GPU/GPUEB и SMMU protection tables. Для Mu эти диапазоны следует считать недоступными до отдельного доказательства обратного.

## Четыре динамических узла

| Узел | Ограничения DT | Runtime-размещение | Подтверждение и предел |
|---|---|---|---|
| `ccci-dpmaif-cache-memory` | size `0x190000`, align `0x1000`, `[0x40000000,0x130000000)` | `[0x12fe20000,0x12ffb0000)` | Точная строка `/proc/iomem`; вместе со следующим узлом составляет единую точную `memblock.reserved` `[0x12fe20000,0x130000000)` |
| `ccci-dpmaif-nocache-memory` | size `0x50000`, align `0x1000`, тот же alloc-range, `no-map` | `[0x12ffb0000,0x130000000)` | Верхнеуровневый `reserved` начинается ровно на границе cache/nocache; `memblock` объединяет пару |
| `ssmr-reserved-cma-memory` | size `0x8000000`, align `0x1000000`, `[0x80000000,0x140000000)`, `reusable` | `[0x138000000,0x140000000)` | Прямое именованное доказательство: CMA `base_pfn=1277952`, `count=32768`; границы также точно совпадают с `/proc/iomem` и `memblock.reserved` |
| `mkp-kernel-code-protection-memory` | size `0x10000`, alloc-range ровно `[0x40000000,0x40010000)` | `[0x40000000,0x40010000)` | Единственно возможная база; точная дочерняя строка `/proc/iomem`; поздний `memblock` уже объединяет её с runtime kernel reservation |

Для SSMR слово «кандидат» больше не требуется: имя каталога CMA и PFN задают владельца и границы напрямую. Для двух DPMAIF областей и MKP `/proc/iomem` пишет только `reserved`, без phandle владельца. Их адреса не угаданы: других интервалов с теми же size/alignment/alloc-range в снимке нет, но именованная ранняя трассировка сохранила бы последнее различие между уникальным сопоставлением и прямой записью владельца.

`CmaTotal=144 MiB` в точности равен двум каталогам CMA. `ssmr-reserved-cma-memory`: `base_pfn=1277952`, `count=32768`, то есть `[0x138000000,0x140000000)` размером 128 MiB; на момент снимка `used=0`, `maxchunk=32768` страниц. `reserved`: `base_pfn=1029120`, `count=4096`, то есть `[0xfb400000,0xfc400000)` размером 16 MiB; `used=4080`, `maxchunk=16` страниц. Обе области имеют `order_per_bit=0`. PFN рассчитаны при засвидетельствованном размере страницы 4096 байт и точно совпадают с `/proc/iomem`; вторая область также совпадает с `CONFIG_CMA_SIZE_MBYTES=16`.

## 29 интервалов `mblock_info`: «дыры» между статическими carveout

| # | Base | End | Size | Rank |
|---:|---:|---:|---:|---:|
| 0 | `0x40000000` | `0x48080000` | `0x8080000` | 0 |
| 1 | `0x48180000` | `0x48401000` | `0x281000` | 0 |
| 2 | `0x48402000` | `0x48800000` | `0x3fe000` | 0 |
| 3 | `0x48a00000` | `0x6fffe000` | `0x275fe000` | 0 |
| 4 | `0x6ffff000` | `0x7a000000` | `0xa001000` | 0 |
| 5 | `0x7b530000` | `0x7be00000` | `0x8d0000` | 0 |
| 6 | `0x7fe00000` | `0x7fe70000` | `0x70000` | 0 |
| 7 | `0x7fff0000` | `0x7fffb000` | `0xb000` | 0 |
| 8 | `0x80100000` | `0x87e00000` | `0x7d00000` | 0 |
| 9 | `0x89f20000` | `0x8a000000` | `0xe0000` | 0 |
| 10 | `0x8c560000` | `0x8e000000` | `0x1aa0000` | 0 |
| 11 | `0x8e130000` | `0x8ed50000` | `0xc20000` | 0 |
| 12 | `0x8f898000` | `0x8f900000` | `0x68000` | 0 |
| 13 | `0x90000000` | `0x9f06f000` | `0xf06f000` | 0 |
| 14 | `0x9ffff000` | `0xb8000000` | `0x18001000` | 0 |
| 15 | `0xb9f00000` | `0xbf500000` | `0x5600000` | 0 |
| 16 | `0xbfd00000` | `0xbfdf0000` | `0xf0000` | 0 |
| 17 | `0xce000000` | `0xd0000000` | `0x2000000` | 0 |
| 18 | `0xd33c0000` | `0xd3400000` | `0x40000` | 0 |
| 19 | `0xd3650000` | `0xd4000000` | `0x9b0000` | 0 |
| 20 | `0xdb3c0000` | `0xed780000` | `0x123c0000` | 0 |
| 21 | `0xee000000` | `0xf8000000` | `0xa000000` | 0 |
| 22 | `0xf8810000` | `0xfc400000` | `0x3bf0000` | 0 |
| 23 | `0xfc700000` | `0xfc800000` | `0x100000` | 0 |
| 24 | `0xfca80000` | `0xfcbef000` | `0x16f000` | 0 |
| 25 | `0xfffff000` | `0x1bfffe000` | `0xbffff000` | 0 |
| 26 | `0x1c0000000` | `0x33d600000` | `0x17d600000` | 1 |
| 27 | `0x33d641000` | `0x33d800000` | `0x1bf000` | 1 |
| 28 | `0x33ff00000` | `0x33ffbe000` | `0xbe000` | 1 |

Это не «свободные Linux holes». Например, интервал 0 содержит kernel/DTB, интервал 3 содержит initrd и BL33 lifetime, интервал 25 содержит обе DPMAIF области и SSMR, а интервал 26 содержит крупные поздние `memblock.reserved` allocations. Их единственный доказанный смысл — свободный список LK после удаления 52 постоянных carveout на момент формирования свойства.

Поздний `memblock.memory` содержит 60 записей, union которых снова равен всем 12 GiB. Записи разрезаны на границах `no-map` и других регионов, но этот debugfs text не показывает flags. Поэтому присутствие там также не означает allocatable.

## Boot payload и изменение lifetime

| Домен | Диапазон | Факт |
|---|---|---|
| LK `mb_kernel` | `[0x40000000,0x47c80000)` (`0x7c80000`) | Точный `mblock-R`; vendor boot header также задаёт kernel load address `0x40000000` |
| Linux kernel code | `[0x40010000,0x41bc0000)` | `/proc/iomem`; первые `0x10000` заняты MKP protection candidate |
| Linux kernel data | `[0x41fd0000,0x42270000)` | `/proc/iomem`; между code/data остаётся child `reserved` `[0x41bc0000,0x41fd0000)` |
| Поздний kernel memblock | `[0x40000000,0x4226a000)` и `[0x4226d000,0x42270000)` | Точные runtime записи; небольшой разрыв нельзя считать безопасным без владельца/lifetime |
| LK `mb_dtb` | `[0x47c80000,0x48080000)` (`0x400000`) | Точный `mblock-R`; vendor boot header даёт тот же DTB address |
| Живой DTB | `[0x47c80000,0x47cf81a2)` (`0x781a2`) | Точная поздняя `memblock.reserved`; `/proc/iomem` округляет до `[0x47c80000,0x47cf9000)` |
| LK `mb_ramdisk` | `[0x66f00000,0x6af00000)` (`0x4000000`) | Точный `mblock-R`; vendor boot header даёт ramdisk load address `0x66f00000` |
| chosen initrd | `[0x66f00000,0x6ad5012e)` | Живые `linux,initrd-start/end`; `/proc/iomem` округляет конец до `0x6ad51000` |
| Поздний initrd memblock | `[0x6ad50000,0x6ad51000)` | К моменту снимка memblock осталась только последняя страница, хотя `/proc/iomem` ещё показывает исходный диапазон |
| PL buffers | `[0x48500000,0x48600000)`, `[0x48600000,0x48700000)` | `pl-drambuf` и `pl-boottag` в `mblock-R`; Linux затем показывает их как System RAM без child reservation |
| BL33 | `[0x50700000,0x62700000)` (`0x12000000`) | `mblock-R`; Linux затем показывает System RAM |
| BL2 extension | `[0x78000000,0x7a000000)` | LK allocation и явный `mblock_free_with_size` перед handoff |
| Временный BL31 DTB | `[0x7fe00000,0x7ff00000)` | LK allocation и явный free; позднее часть адресов занята постоянным GPUEB carveout |
| AVB/logo scratch | меняющиеся высокие адреса, см. JSON events | LK показывает allocations и несколько frees; кольцевой журнал не содержит полного конечного списка каждого scratch allocation |

Ключевое расхождение намеренное и полезное: `mblock_info` снова включает kernel/DTB/initrd адреса, хотя они нужны во время перехода. В доступном LK source этот механизм называется возвратом boot image mblocks перед входом в kernel. Следовательно, даже точный mblock free list описывает политику конкретного handoff, а не физическую пустоту.

Бинарное свойство `/chosen/atag,boot` сохранено в `live-fdt.json`, но доступные снимки не дают binding для его полей. Аудит не извлекает из него адреса. Живые `linux,initrd-start/end`, заголовок активного `vendor_boot_b`, `/proc/iomem` и точные LK/memblock строки дают независимо проверяемые границы boot payload без догадок о vendor ATAG.

## Остаточные Linux reservations

После вычитания 52 fixed carveout, четырёх динамических runtime-размещений, initrd и DTB из union всех строк `reserved` `/proc/iomem` остаются:

| Диапазон | Size | Наблюдение |
|---|---:|---|
| `[0x41bc0000,0x41fd0000)` | `0x410000` | kernel-internal child reserved между строками code/data |
| `[0x47fff000,0x48000000)` | `0x1000` | memblock уточняет занятые байты до `[0x47fff700,0x48000000)` |
| `[0xfb400000,0xfc400000)` | `0x1000000` | прямо подтверждён CMA `reserved`: `base_pfn=1029120`, `count=4096` |
| `[0x32d400000,0x33be00000)` | `0xea00000` | точная крупная `memblock.reserved`, владелец в снимке не назван |
| `[0x33be4a000,0x33bf3b000)` | `0xf1000` | iomem page envelope; memblock раскрывает несколько точных субдиапазонов |
| `[0x33bf3c000,0x33d600000)` | `0x16c4000` | iomem envelope; memblock содержит множество субстраничных записей и длинный хвост |
| `[0x33d641000,0x33d800000)` | `0x1bf000` | часть того же высокого runtime reserve после вычитания `gz-log` |
| `[0x33ff00000,0x33ffbe000)` | `0xbe000` | memblock резервирует `[0x33ff00000,0x33fffe000)` целиком; конец — fixed `log_store` |

Полный точный список 123 `memblock.reserved` записей сохранён в JSON. Ни одна из остаточных строк не считается свободной. Высокие интервалы могут быть kernel metadata, pools или driver allocations; без имени источника это остаётся неизвестным.

## Пересечения и границы

- Между 52 fixed carveout нет пересечений; соседние регионы объединяют union до 29 сегментов.
- Между четырьмя динамическими runtime-размещениями нет пересечений. Cache и nocache DPMAIF соприкасаются в `0x12ffb0000`.
- Между fixed и dynamic размещениями нет пересечений.
- LK boot reservation `mb_kernel` содержит динамический MKP protection candidate `[0x40000000,0x40010000)`; фактическая строка kernel code начинается после него.
- Kernel code/data, DTB и initrd лежат внутри интервалов, которые позднее снова присутствуют в `mblock_info`. Это пересечение разных стадий и lifetime, а не конфликт статических carveout.
- Все адреса firmware по именам carveout считаются отдельными protection domains. `map:1` в LK или отсутствие `no-map` только говорит о политике отображения той стадии и не предоставляет ownership Mu.

## Неизбежные измерения на фактическом входе в Mu

Для выбора FD/stack/heap минимальному payload нужны именно его boot-time факты:

1. Снять активный mblock reserved/free list в точке запуска Mu и одновременно записать адрес/размер загруженного Mu image, relocation target, текущий stack и page tables. Это должно произойти до любого возврата или переиспользования соответствующих boot mblocks.
2. Зафиксировать контракт замены BL33: кто передаёт управление Mu и какие из `mb_kernel`, `mb_dtb`, `mb_ramdisk`, `pl-drambuf`, `pl-boottag` к этому моменту ещё живы либо уже освобождены. Linux post-boot карта не восстанавливает этот lifetime.
3. В той же точке, если текущий EL разрешает чтение соответствующих регистров, записать EL, translation regime, cache state, MAIR/TCR/TTBR и physical-address width. Допустимость доступа к выбранной области подтвердить существующим handoff/firewall contract; намеренно читать защищённые carveout для пробы нельзя. Linux `CONFIG_ARM64_PA_BITS=48` и доступность RAM в EL1 этого не доказывают.
4. Получить доказательство ownership выбранной области одним из доступных механизмов: разместить payload внутри явно переданного ему LK reservation, добавить documented handoff reservation либо использовать доступный allocator этой boot stage. Затем проверить отсутствие пересечений с image/stack/page tables/DMA. Ограниченный destructive memory test допустим только внутри уже доказанно принадлежащего payload резерва.
5. Сформировать UEFI descriptors по измеренным ownership и attributes. Большой `System RAM` или интервал старого mblock list нельзя целиком маркировать `EfiConventionalMemory`.

Именованный ранний `of_reserved_mem` log для DPMAIF/MKP и владельцы поздних Linux-only резервов `0x32d400000..0x33be00000` и `0x33be4a000..0x33d800000` улучшат атрибуцию полного Android runtime. Они не обязательны для минимального Mu payload, если payload исключает все эти интервалы и не заявляет их conventional memory. SSMR и обе CMA области уже имеют прямое именованное runtime-доказательство.

До этих boot-time измерений конкретный FD base, stack base и heap arena остаются неутверждёнными.
