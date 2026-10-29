# Аудит целей сборки Mu-Silicium

Дата аудита: 2026-09-14. Проверяемая ревизия верхнего дерева Mu-Silicium: `964a886f3d1ad3f35137435b11287c8d6827437b`.

## Вывод

В дереве нет цели `degas` и пакета MT6897. Существующие `stonePkg` и `lancelotPkg` можно использовать только как донорские **графы компиляции** общих модулей. Успешная сборка через них доказывает совместимость изменённых файлов с реальными EDK II/Mu headers, AutoGen, библиотечными классами и AArch64 toolchain. Она не делает полученный образ прошивкой для Xiaomi 14T и не подтверждает адреса, карту памяти или аппаратное поведение degas.

Наиболее узкий достоверный маршрут сейчас — две DEBUG module-only сборки через `stonePkg`:

1. `SiliciumPkg/Sec/Sec.inf` — компилирует изменённый SEC и в DEBUG подтягивает изменённый `FrameBufferSerialPortLib` как экземпляр `SerialPortLib`;
2. `SiliciumPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf` — отдельно компилирует и линкует изменённый GOP driver.

`stonePkg` предпочтительнее `lancelotPkg` только для этой проверки: он покрывает запрошенный для аудита граф MT6789, а в его собственном DSC не включена ACPI-таблица. Сходство цифр в названиях MT6789 и MT6897 не является техническим доказательством родства; оно не подтверждает общность регистров или возможность запускать результат на degas.

## Почему module-only путь настоящий

`stonePkg/DeviceBuild.py` и `lancelotPkg/DeviceBuild.py` являются Stuart entry points. Оба задают `ACTIVE_PLATFORM`, `TARGET_ARCH=AARCH64`, `TOOL_CHAIN_TAG=CLANGPDB` и передают `BLD_*_FD_BASE`, `BLD_*_FD_SIZE` и `BLD_*_FD_BLOCKS` в DSC/FDF.

В `SiliciumPkg/SiliciumPkg.dsc.inc`:

- `SiliciumPkg/Sec/Sec.inf` находится в `[Components]`;
- `SiliciumPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf` находится в `[Components]`;
- при `TARGET=DEBUG` класс `SerialPortLib` привязан к `SiliciumPkg/Library/FrameBufferSerialPortLib/FrameBufferSerialPortLib.inf`;
- при `TARGET=RELEASE` вместо него выбирается null library, поэтому RELEASE не проверяет изменённую framebuffer console library.

Stuart `UefiBuilder` распознаёт переменную `BUILDMODULE`, добавляет к EDK II build command параметр `-m <INF>`, отключает post-build и не вызывает flash path. Это именно переменная `BUILDMODULE`; семейство `BLD_*` служит для `-D` макросов DSC/FDF и не выбирает модуль.

Отдельная сборка INF самой `FrameBufferSerialPortLib` не нужна для основного результата: сборка `Sec.inf` проверяет её в реальном контексте потребителя и линкует вместе с SEC. Её можно запустить как дополнительную диагностику, если понадобится локализовать ошибку компиляции библиотеки.

## Команды в WSL

Команды ниже ничего не упаковывают в Android boot image и ничего не записывают на телефон. `--setup`/`--update` нужны один раз после получения recursive submodules; если текущий процесс подготовки уже успешно их выполнил, повторять их не нужно.

```bash
cd /mnt/d/Edk2-For-Xiaomi14T/mu

python3 -m venv .venv-build
source .venv-build/bin/activate
python3 -m pip install -r pip-requirements.txt

export CLANGPDB_AARCH64_PREFIX=aarch64-linux-gnu-

python3 Platforms/Xiaomi/stonePkg/DeviceBuild.py --setup -t DEBUG
python3 Platforms/Xiaomi/stonePkg/DeviceBuild.py --update -t DEBUG

python3 Platforms/Xiaomi/stonePkg/DeviceBuild.py \
  TARGET=DEBUG \
  ENABLE_SECUREBOOT=0 \
  FD_BASE=0x40000000 \
  FD_SIZE=0x200000 \
  FD_BLOCKS=0x200 \
  DEVICE_MODEL=0 \
  BUILDMODULE=SiliciumPkg/Sec/Sec.inf

python3 Platforms/Xiaomi/stonePkg/DeviceBuild.py \
  TARGET=DEBUG \
  ENABLE_SECUREBOOT=0 \
  FD_BASE=0x40000000 \
  FD_SIZE=0x200000 \
  FD_BLOCKS=0x200 \
  DEVICE_MODEL=0 \
  BUILDMODULE=SiliciumPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf
```

Значения `FD_*`, stack base, memory map и framebuffer geometry в этой команде и `stone.dsc` — данные donor target, необходимые его build graph. Полученные файлы нельзя переименовывать в degas image или передавать в `fastboot boot`.

Дополнительная локализующая команда:

```bash
python3 Platforms/Xiaomi/stonePkg/DeviceBuild.py \
  TARGET=DEBUG \
  ENABLE_SECUREBOOT=0 \
  FD_BASE=0x40000000 \
  FD_SIZE=0x200000 \
  FD_BLOCKS=0x200 \
  DEVICE_MODEL=0 \
  BUILDMODULE=SiliciumPkg/Library/FrameBufferSerialPortLib/FrameBufferSerialPortLib.inf
```

Если BaseTools отвергнет прямую сборку library INF как самостоятельного module, это не дефект библиотеки: основной критерий остаётся успешная сборка `Sec.inf`, где она является library instance.

Не следует использовать для этой проверки `python3 build_uefi.py -d stone`: orchestration script удаляет и заново накладывает patches на `Mu_Basecore`, собирает полный donor FD и затем формирует Android image. Эти действия шире требуемой compile validation.

## Зависимости и ожидаемые препятствия

Для Ubuntu/WSL нужны как минимум пакеты, перечисленные в `setup_env.sh`: `build-essential`, `uuid-dev`, `nasm`, `gcc-aarch64-linux-gnu`, `python3`, `python3-venv`, `python3-pip`, `gettext`, `clang`, `llvm`, `lld`, а для Stuart external dependencies также `mono-devel`, `git`, `curl`, certificates и GnuPG. Python packages закреплены в `pip-requirements.txt`, включая `edk2-pytool-extensions~=0.31.1` и `edk2-pytool-library~=0.23.16`.

Перед сборкой должны быть заполнены git submodules `Common/Mu`, `Common/Mu_OEM_Sample`, `Mu_Basecore`, `Binaries`, `Silicium-ACPI` и вложенный OpenSSL. Во время аудита `Mu_Basecore` ещё содержал только `.git`, а `edk2toolext` ещё не импортировался ни в WSL, ни в Windows Python; это состояние подготовки, а не ошибка исходников.

`CLANGPDB_AARCH64_PREFIX=aarch64-linux-gnu-` нужно экспортировать в том же shell, где запускается build. Скрипт `setup_env.sh` экспортирует его только в собственный процесс, поэтому запуск скрипта как `./setup_env.sh` не сохраняет переменную в родительском shell.

Сборка на `/mnt/d` может быть заметно медленнее native Linux filesystem. Это влияет на время, но не на смысл проверки.

После успеха следует сохранить:

- полный stdout/stderr каждой команды;
- `Build/stonePkg/DEBUG_CLANGPDB/BUILD_REPORT.TXT`, если Stuart создал report для module-only run;
- пути и SHA-256 полученных `Sec`/`SimpleFbDxe` build artifacts;
- строку `git rev-parse HEAD` верхнего Mu и status изменённых shared files.

В логе сборки `Sec.inf` должно быть видно создание объекта `FrameBufferSerialPortLib.c`; одного успешного `Sec.efi` недостаточно, если incremental cache был создан до изменения библиотеки. Для окончательной проверки лучше удалить только соответствующий `Build/stonePkg` output либо запустить Stuart clean option после того, как станет известен точный CLI установленной версии.

## Что проверяет сборка и чего она не проверяет

Положительный результат проверяет:

- парсинг изменённых INF/DEC/DSC declarations, включая `PcdFrameBufferPixelsPerScanLine`;
- компиляцию AArch64 с реальными Mu/EDK II headers;
- разрешение library classes и линковку SEC/SimpleFbDxe;
- отсутствие обычных compile/link regressions от изменений stack argument, stride, bounds checks и cursor storage.

Результат не проверяет:

- MT6897 entry state, EL, MMU/cache/coherency и writable data SEC;
- размещение FD, stack или DXE heap для degas;
- ownership либо lifetime любого диапазона RAM;
- фактический scanout base, pixel format, cache policy, command-mode refresh и видимость записи на панели;
- возможность загрузить donor FD на Xiaomi 14T.

Ограничения согласуются с `FRAMEBUFFER-CONTRACT.md` и `MEMORY-RECONCILIATION.md`: stride `1248` и видимая геометрия `1220 x 2712 x 32` установлены, но это не утверждает firmware layout и не предоставляет Mu область для FD/stack/heap.

## Безопасная будущая compile-check цель

Да, отдельную цель можно создать до утверждения degas FD layout. Она должна быть явно названа, например, `CompileCheckPkg`, а не `degasPkg`, и не должна иметь `FLASH_DEFINITION` или `.fdf`.

Минимальная цель должна:

- поддерживать только `AARCH64` и `DEBUG`;
- включать настоящие Mu/EDK II packages и общие library-class bindings;
- содержать в `[Components]` только `Sec.inf` и `SimpleFbDxe.inf` либо использовать `BUILDMODULE` для каждого из них;
- задавать `PcdFrameBufferWidth=1220`, `PcdFrameBufferHeight=2712`, `PcdFrameBufferColorDepth=32` и `PcdFrameBufferPixelsPerScanLine=1248` как compile-test values;
- привязать `PlatformSecLib` к null implementation, чтобы не тащить донорскую watchdog MMIO запись;
- использовать небольшой test-only `MemoryMapLib` со строго синтетическими descriptors, достаточными для компиляции; никакие его адреса не должны объявляться данными degas;
- выпускать только module artifacts в отдельный `Build/CompileCheckPkg`, без FD, boot image и post-build упаковки.

Синтетические `PcdFdBaseAddress`, `PcdFdSize` и stack values могут понадобиться AutoGen для `Sec.inf`, но они должны быть помечены test-only и не попадать в `.fdf`. Такая цель проверит вариант с явным stride `1248` на уровне настоящего build graph, не требуя выбрать место для прошивки в RAM.

## QEMU

В верхнем Mu-Silicium дереве собственной QEMU/ArmVirt build target не найдено. На момент аудита checkout `Mu_Basecore` ещё не был заполнен, поэтому наличие и точное имя возможного ArmVirt target в закреплённом commit `7e2a44a7129856365a8d71eff83de38877b47e66` не подтверждено. После завершения submodule checkout это проверяется без предположений:

```bash
cd /mnt/d/Edk2-For-Xiaomi14T/mu
rg --files Mu_Basecore | rg -i '(^|/)(ArmVirt|Qemu|QEMU).*(\.dsc|PlatformBuild\.py|DeviceBuild\.py)$'
```

Даже если Basecore содержит ArmVirt/QEMU DSC, он сам по себе не является runtime test для этих изменений: Silicium modules должны быть добавлены в его DSC, `MemoryMapLib` должен предоставлять именованный `Display Reserved` RAM range, а SEC entry path существенно отличается от телефонного LK handoff. Поэтому QEMU можно рассматривать как последующий тест алгоритмов на mock framebuffer, но не как подтверждение degas bootability.
