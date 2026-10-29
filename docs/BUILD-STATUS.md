# Сборка 2026-09-14

Полная контрольная сборка stone RELEASE завершилась успешно: `artifacts/control-stone/STONE_CONTROL_ONLY.fd`, 2097152 байта. Проверены размер, сигнатура FV, длина и checksum заголовка. SHA-256: `cf5f665ffc83c10e0088cf13debff06ee330ff06f180b86b7b0cfff649b2f2cb`. Журнал: `evidence/build/mu-control-fd.log`. Это донорская прошивка только для проверки сборочного процесса на ПК, не образ для degas.

Упакованный FV занимает 482176 байт из 2097152. Внутренний несжатый FVMAIN заполнен на 99% (свободно 1976 байт); при добавлении модулей потребуется пересмотреть его размер. Аппаратных проверок не было.

Успешно собраны реальные модули Mu-Silicium `Sec.efi` (61440 байт) и `SimpleFbDxe.efi` (21504 байт): DEBUG, CLANGPDB, AARCH64. Они находятся в `artifacts/mu-module-check/`. Формат PE32+ ARM64, хеши и фактические параметры AutoGen проверены скриптом `tools/record_mu_build.py`; отчёт — `evidence/mu-build-validation-2026-09-14.json`.

Оба модуля используют геометрию degas: 1220×2712, 32 бита, PixelsPerScanLine=1248 (4992 байта). Для разрешения зависимостей используется существующий пакет stone. Его карта памяти и аппаратные библиотеки остаются донорскими. Эти файлы не являются прошивкой для Xiaomi 14T и не запускались на телефоне.

Среда: WSL Ubuntu 26.04, Clang/LLD 21.1.8, GCC AArch64 15.2.0, Python 3.14.4, venv `/home/gavso/.venvs/degas-mu`. BaseTools собраны из закреплённого Mu_Basecore с отдельной const-правкой EfiRom для GCC 15. Все шесть основных submodules и девять вложенных Mu_Basecore закреплены по gitlink; Binaries использует sparse checkout generic. Манифесты зависимостей и журналы — в `evidence/build/` и `evidence/toolchain-dependencies.json`.

Повтор сборки модулей в текущей среде:

```powershell
wsl -- bash /mnt/d/Edk2-For-Xiaomi14T/tools/build_mu_modules.sh
python tools/record_mu_build.py
```

Для последующей контрольной сборки полного донорского FD применены штатные patches из `mu/Resources/MuPatches/`: Auth-Service, Boot-Manager, Timer, Usb-Bus. Первый успешный DEBUG module-only проход выполнен до их применения. Контрольный режим запускается аргументом `control-fd` и использует RELEASE и исходные PCD stone. Он не создаёт Android boot container.

Сборщик сообщает существующее предупреждение stone: ArmPsciResetSystemLib не объявляет класс HwResetSystemLib. Успех компиляции не подтверждает работу reset, дисплея, MMU или передачи управления LK. Для собственного пакета degas остаются неподтверждёнными размещение FD/stack/heap и контракт входа; адреса донора для этого не выбирались.
