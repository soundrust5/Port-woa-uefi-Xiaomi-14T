# Source snapshot / Исходники

## English

`mu/` contains the Mu-Silicium working source snapshot used during Xiaomi 14T development, based on upstream revision `964a886f3d1ad3f35137435b11287c8d6827437b` of [Project-Silicium/Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium).

- Xiaomi 14T platform: `mu/Platforms/Xiaomi/degasPkg/`.
- Three shared touch transport/protocol headers: `diagnostic/degas/touch/`.
- Upstream dependencies are pinned Git submodules in the root `.gitmodules` file.
- Local `Mu_Basecore` changes are preserved as `source-patches/Mu_Basecore/local-changes.patch`; additional source files are under its `new-files/` directory and retain their relative paths.
- The shared firmware contains earlier platform work as well as Xiaomi 14T changes. AI assistance applies to the custom port development, not authorship of upstream Mu/EDK2.

For source inspection, clone recursively. To reconstruct the local Basecore source after initializing submodules, apply `local-changes.patch` inside `mu/Mu_Basecore`, then copy the contents of `new-files/` into that same directory, preserving paths. These are source reconstruction notes, **not device installation instructions**. This snapshot is not a supported standalone build or install release.

Original copyright notices and licenses are retained, including [Mu-Silicium's license](mu/LICENSE). Dependencies and individual files retain their own licenses. There is no replacement blanket license for third-party components.

Build outputs, firmware images, Windows images, phone dumps and private signing keys are not included.

## Русский

В `mu/` находится снимок рабочих исходников Mu-Silicium, использованных при разработке порта Xiaomi 14T. Основа — ревизия `964a886f3d1ad3f35137435b11287c8d6827437b` проекта [Project-Silicium/Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium).

- Платформа Xiaomi 14T: `mu/Platforms/Xiaomi/degasPkg/`.
- Три общих заголовка транспорта и протокола тача: `diagnostic/degas/touch/`.
- Внешние зависимости закреплены как Git-подмодули в корневом `.gitmodules`.
- Локальные изменения `Mu_Basecore` сохранены в `source-patches/Mu_Basecore/local-changes.patch`; дополнительные исходники — в папке `new-files/` с сохранением относительных путей.
- Общие исходники прошивки содержат работу над предыдущими платформами и Xiaomi 14T. Участие ИИ относится к разработке нашего порта, а не к авторству исходной базы Mu/EDK2.

Для изучения исходников клонируйте репозиторий рекурсивно. Чтобы восстановить рабочую версию Basecore после загрузки подмодулей, примените `local-changes.patch` внутри `mu/Mu_Basecore`, затем скопируйте туда содержимое `new-files/` с сохранением путей. Это описание восстановления исходников, **не инструкция по установке на телефон**. Снимок не является поддерживаемым самостоятельным выпуском для сборки или установки.

Исходные уведомления об авторских правах и лицензии сохранены, включая [лицензию Mu-Silicium](mu/LICENSE). Для зависимостей и отдельных файлов действуют их собственные лицензии.

Результаты сборки, образы прошивки и Windows, дампы телефона и закрытые ключи подписи не включены.
