# Объявленные диапазоны памяти Xiaomi 14T

Дополнение после первого прохода: [сверка памяти и резервов](MEMORY-RECONCILIATION.md), [контракт framebuffer](FRAMEBUFFER-CONTRACT.md). Эти документы содержат позднейшие данные PL/LK, memblock, CMA и точного установленного драйвера.

Источник: живой FDT, полученный 2026-09-14. Это реестр DT, не готовая MemoryMapLib Mu и не список свободной памяти.

SHA-256 FDT: `0bfe978cc3792e0734681f9b3cac168ccfb600fb4e3cb87ebcf8091d5282b11a`.

Диапазоны используют исключительную верхнюю границу. Любая строка требует проверки владельца, жизненного цикла и доступа CPU. `no-map`/`reusable` — свойства DT, а не готовый UEFI memory type. Отсутствие `no-map` не даёт права перезаписывать регион.

## DRAM

| Узел | Base | End exclusive | Size | no-map | reusable |
|---|---|---|---|---|---|
| `/memory` | `0x40000000` | `0x340000000` | `0x300000000` | False | False |

## Reserved-memory с reg

| Узел | Base | End exclusive | Size | no-map | reusable |
|---|---|---|---|---|---|
| `/reserved-memory/mblock-52-sspm_ap-shared` | `0x9f06f000` | `0x9f17f000` | `0x110000` | True | False |
| `/reserved-memory/mblock-51-platform-debug_slbc_shared` | `0xfcbef000` | `0xfcc2f000` | `0x40000` | True | False |
| `/reserved-memory/mblock-50-me_GPUEB_SHARED` | `0x7fe70000` | `0x7fff0000` | `0x180000` | True | False |
| `/reserved-memory/mblock-49-me_CSFFWLOG_reserved` | `0x7fffb000` | `0x7ffff000` | `0x4000` | True | False |
| `/reserved-memory/mblock-48-adsp_shared_reserved` | `0xf8000000` | `0xf8810000` | `0x810000` | True | False |
| `/reserved-memory/mblock-47-debug_met_res-ram` | `0x9f17f000` | `0x9fd7f000` | `0xc00000` | True | False |
| `/reserved-memory/mblock-46-scp_emi_reserved` | `0x89f00000` | `0x89f20000` | `0x20000` | True | False |
| `/reserved-memory/mblock-45-shared-dma-pool_wifi-reserve-memory_dma` | `0x88b00000` | `0x89f00000` | `0x1400000` | True | False |
| `/reserved-memory/mblock-44-consys_emi_reserved` | `0x87e00000` | `0x88b00000` | `0xd00000` | True | False |
| `/reserved-memory/mblock-43-me_GPUmputab_PMA` | `0x7e800000` | `0x7ee00000` | `0x600000` | True | False |
| `/reserved-memory/mblock-42-me_vcp_reserved` | `0xbf500000` | `0xbfd00000` | `0x800000` | True | False |
| `/reserved-memory/mblock-41-adsp_image_reserved` | `0x7a000000` | `0x7b530000` | `0x1530000` | True | False |
| `/reserved-memory/mblock-40-md_mem_usage` | `0xed780000` | `0xee000000` | `0x880000` | True | False |
| `/reserved-memory/mblock-39-md_mem_usage` | `0xd4000000` | `0xdb3c0000` | `0x73c0000` | True | False |
| `/reserved-memory/mblock-38-md_mem_usage` | `0xd3400000` | `0xd3650000` | `0x250000` | True | False |
| `/reserved-memory/mblock-37-ap_md_c_smem` | `0x8a000000` | `0x8c560000` | `0x2560000` | True | False |
| `/reserved-memory/mblock-36-md_mem_usage` | `0xd0000000` | `0xd33c0000` | `0x33c0000` | True | False |
| `/reserved-memory/mblock-35-ccci_tag_mem` | `0xbfdf0000` | `0xbfe00000` | `0x10000` | True | False |
| `/reserved-memory/mblock-34-apu_sapu_apurvgzctrlshm` | `0x8f900000` | `0x8fa00000` | `0x100000` | True | False |
| `/reserved-memory/mblock-33-apu_apusys-rv_aee-coredump` | `0x8ed50000` | `0x8f000000` | `0x2b0000` | True | False |
| `/reserved-memory/mblock-32-apu_apusys-rv_secure` | `0x8fa00000` | `0x90000000` | `0x600000` | True | False |
| `/reserved-memory/mblock-31-mm_ccu_reserved` | `0x80000000` | `0x80100000` | `0x100000` | True | False |
| `/reserved-memory/mblock-30-reserve-memory-scp_share` | `0x8f000000` | `0x8f898000` | `0x898000` | True | False |
| `/reserved-memory/mblock-29-SCP-reserved` | `0xb8000000` | `0xb9f00000` | `0x1f00000` | True | False |
| `/reserved-memory/mblock-28-ap_md_nc_smem` | `0x8e000000` | `0x8e130000` | `0x130000` | True | False |
| `/reserved-memory/mblock-27-platform-debug_dfdsoc_dump` | `0xfc400000` | `0xfc700000` | `0x300000` | True | False |
| `/reserved-memory/mblock-26-tee-shm` | `0x7f000000` | `0x7f400000` | `0x400000` | True | False |
| `/reserved-memory/mblock-25-tee-reserved` | `0xc0000000` | `0xce000000` | `0xe000000` | True | False |
| `/reserved-memory/mblock-24-tee-secmem` | `0x7f400000` | `0x7fd00000` | `0x900000` | True | False |
| `/reserved-memory/mblock-23-MCUPM-reserved` | `0x7fd00000` | `0x7fe00000` | `0x100000` | True | False |
| `/reserved-memory/mblock-22-SSPM-reserved` | `0x9fd7f000` | `0x9ffff000` | `0x280000` | True | False |
| `/reserved-memory/mblock-21-platform_mtksmmu_protpgd` | `0x7be00000` | `0x7e000000` | `0x2200000` | True | False |
| `/reserved-memory/mblock-20-gz-log` | `0x33d600000` | `0x33d641000` | `0x41000` | False | False |
| `/reserved-memory/mblock-19-unmap2` | `0x33d800000` | `0x33da00000` | `0x200000` | True | False |
| `/reserved-memory/mblock-18-mkp` | `0x33da00000` | `0x33dc00000` | `0x200000` | True | False |
| `/reserved-memory/mblock-17-gz` | `0x33dc00000` | `0x33fe00000` | `0x2200000` | True | False |
| `/reserved-memory/mblock-16-me_cmdq_reserved` | `0x33fe00000` | `0x33ff00000` | `0x100000` | True | False |
| `/reserved-memory/mblock-15-framebuffer` | `0xfcc2f000` | `0xfffff000` | `0x33d0000` | False | False |
| `/reserved-memory/mblock-14-me_gpu_reserved` | `0x7ee00000` | `0x7f000000` | `0x200000` | True | False |
| `/reserved-memory/mblock-13-security_dice_common` | `0x6fffe000` | `0x6ffff000` | `0x1000` | True | False |
| `/reserved-memory/mblock-12-atf-log-reserved` | `0xbfe00000` | `0xc0000000` | `0x200000` | True | False |
| `/reserved-memory/mblock-11-log_store` | `0x33ffbe000` | `0x33fffe000` | `0x40000` | False | False |
| `/reserved-memory/mblock-10-emi_mbist_buf` | `0x7ffff000` | `0x80000000` | `0x1000` | True | False |
| `/reserved-memory/mblock-9-dramc-rk1` | `0x33fffe000` | `0x340000000` | `0x2000` | True | False |
| `/reserved-memory/mblock-8-dramc-rk0` | `0x1bfffe000` | `0x1c0000000` | `0x2000` | True | False |
| `/reserved-memory/mblock-7-platform-debug_dfdmcu_dump` | `0xfc800000` | `0xfca80000` | `0x280000` | True | False |
| `/reserved-memory/mblock-6-crypto_hw` | `0x48401000` | `0x48402000` | `0x1000` | True | False |
| `/reserved-memory/mblock-5-aee_lk` | `0x7e000000` | `0x7e800000` | `0x800000` | False | False |
| `/reserved-memory/mblock-4-BL31-reserved` | `0x48800000` | `0x48a00000` | `0x200000` | True | False |
| `/reserved-memory/mblock-3-minirdump` | `0x48170000` | `0x48180000` | `0x10000` | True | False |
| `/reserved-memory/mblock-2-pstore` | `0x48090000` | `0x48170000` | `0xe0000` | True | False |
| `/reserved-memory/mblock-1-aee_debug_kinfo` | `0x48080000` | `0x48090000` | `0x10000` | True | False |

## Узлы size без статического reg

База не установлена этим свойством; сверять с runtime allocation, mblock и iomem. Не подставлять ноль.

- `/reserved-memory/ccci-dpmaif-cache-memory`
- `/reserved-memory/ccci-dpmaif-nocache-memory`
- `/reserved-memory/ssmr-reserved-cma-memory`
- `/reserved-memory/mkp-kernel-code-protection-memory`

## Следующие проверки

Сопоставить все строки с iomem и mblock/ATAG, выяснить вложенные/пересекающиеся carveouts и dynamic allocation. Учесть secure firmware, загруженные образы, cache/MMU и DMA. Лишь затем выбрать FD, stack, heap и оформить UEFI attributes.

Текущие безопасные области для размещения нашего payload: **ещё не утверждены**.
