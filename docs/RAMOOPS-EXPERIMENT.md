# Эксперимент с журналом ramoops

## Подтверждённая конфигурация

Снята с работающего Android после второго опыта: `evidence/raw/ramoops-survey/`.
Ядро: 6.1.138-android14-11-g44bda9e8f6e9. Исходники ram.c, ram_core.c и
pstore_ram.h получены из Android common commit
`44bda9e8f6e9adfc4a5689dcf24bd2e9fa4725fb`; URL и SHA-256 сохранены в
`evidence/sources/ramoops/provenance.json`. Это совпадение с заявленным commit
ядра, а не независимое доказательство полного соответствия бинарника исходникам.

DT reserved-memory, LK и параметры работающего ramoops согласованы:

| Участок | Начало | Конец, не включая | Размер |
|---|---|---|---|
| Вся область pstore | 0x48090000 | 0x48170000 | 0xe0000 |
| dmesg: 31 запись по 0x1000 | 0x48090000 | 0x480af000 | 0x1f000 |
| console | 0x480af000 | 0x480ef000 | 0x40000 |
| ftrace | 0x480ef000 | 0x480f0000 | 0x1000 |
| pmsg | 0x480f0000 | 0x48170000 | 0x80000 |

ECC=0, mem_type=0. CONFIG_PSTORE_RAM/CONSOLE/PMSG=y.
Хотя frontend CONFIG_PSTORE_FTRACE выключен, параметр ftrace_size=4096 и
ramoops_probe всё равно выделяет этот участок. Поэтому начало console нельзя
считать, забыв вычесть ftrace_size.

## Реализация

`diagnostic/ramoops.c` — writer с указателем и длиной, без встроенного физического
адреса. Формат Linux: 12 байт little-endian, sig=0x43474244, start, size, затем
данные кольца. При ECC=0 ёмкость console равна 0x40000-12.
Сигнатура публикуется последней; при прерывании операции журнал может потеряться,
но промежуточные метаданные не выдаются за завершённую запись. Writer однопоточный.
При включённом кэше одной инструкции DSB недостаточно: нужна отдельная политика
mapping/cache clean. Текущий entry принимает только выключенные stage-1 M/C.

`ramoops_probe.c` использует только существующий console-участок, до запуска Android.
Создаёт новый журнал с build ID, EL/SCTLR/base, стадиями и CRC32; затем проверяет
записанные байты чтением. Отдельные WFE-циклы кодируют успех и ошибку публикации.
Они доказывают CPU-visible запись, но не сохранность после reset. Отображение
stage-2 также остаётся отдельной неопределённостью.

`entry_ramoops.S` и `build_ramoops.py` изолированы от сохранённых первых двух образов.
Этот тест не рисует маркер: единственная новая внешняя запись — console-буфер.
Это пока ранний диагностический payload; адаптер Mu DebugLib ещё не подключён.

## Проверка на ПК

Host C tests проверяют формат, wrap, обрезку длинного сообщения и границы буфера.
`diagnostic/tests/test_ramoops_entry.py` исполняет итоговый ELF: EL1/EL2 на двух
базах, отказы M/C/EE/EL3 без записей; проверяет границы console/стека/record,
разбирает данные в порядке persistent_ram_save_old, сверяет build ID и CRC.
Эмуляция не моделирует сохранение DRAM, stage-2 или очистку в LK.

## Условия телефонного опыта

Перед запуском сохранить доступные pstore и резервную копию console из
/data/debuglogger. Новый payload намеренно заменит старую console-запись.
Затем обычный согласованный цикл: boot_b → watchdog → fastboot → восстановление
исходного boot_b → Android. Нельзя отключать питание ради этого опыта.

После восстановления сразу сохранить:
- /sys/fs/pstore/console-ramoops-0 и все остальные pstore-файлы;
- /data/debuglogger/console-ramoops-0 (init копирует туда console после post-fs-data);
- dmesg, pl_lk, полный expdb и свежий HWT из AEE.

Резервный файл debuglogger сейчас датирован 2026-09-10 и сам по себе не доказывает
новый результат. Чтение проверяется командой:

```powershell
python tools/read_probe_ramoops.py ПУТЬ_К_СНЯТОМУ_CONSOLE
```

Успех: извлечён именно build ID текущего образа с корректным CRC32. Отсутствие
файла не доказывает невыполнение кода; сопоставить watchdog PC/LR/SP, затем искать
стадию очистки/потери буфера. Прохождение через fastboot входит в проверяемый путь.

Источник формата: https://android.googlesource.com/kernel/common/+/44bda9e8f6e9adfc4a5689dcf24bd2e9fa4725fb/fs/pstore/ram_core.c
