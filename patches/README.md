# Локальные изменения Mu-Silicium

Исходный репозиторий и точные ревизии указаны в `upstream.json`. Все шесть основных зависимостей и девять вложенных зависимостей `Mu_Basecore` инициализированы; проверка вложенных gitlink хранится в `../evidence/build/nested-dependencies.json`. Для `Binaries` используется sparse checkout каталога `generic`.

| Patch | Куда применяется | Изменение |
|---|---|---|
| `mu-sec-stack-base.patch` | верхний checkout Mu | После очистки стека передаёт в `SecEntry` его начало вместо конца |
| `mu-framebuffer-stride.patch` | верхний checkout Mu | Добавляет независимый PixelsPerScanLine, проверки размеров и границ, размещает курсор вне display carveout |
| `mu-sec-display-preserve.patch` | верхний checkout Mu | Убирает безусловную очистку всего display carveout перед печатью версии |
| `mu-basecore-gcc15-efirom.patch` | `Mu_Basecore` @ `7e2a44a7129856365a8d71eff83de38877b47e66` | Исправляет const-корректность EfiRom для GCC 15 |

Изменения уже внесены в `../mu/`, поэтому повторно применять patches к этому checkout не нужно. Первые три patch относятся только к верхнему Mu; GCC 15 patch применяется отдельно внутри `Mu_Basecore`.

Модули SEC и SimpleFbDxe собраны с геометрией degas. Полная контрольная RELEASE-сборка донорского stone также завершена; это не прошивка degas. Результаты: `../docs/BUILD-STATUS.md`.

Для полной контрольной сборки внутри Mu_Basecore дополнительно применены четыре штатных patch из `mu/Resources/MuPatches`: Auth-Service, Boot-Manager, Timer и Usb-Bus. Их хеши перечислены в `upstream.json`.
