# Прогресс разработки

Каждый чат в конце дописывает сюда свой этап. Следующий чат читает этот файл перед работой.

Формат записи:

## Этап N — название
- Сделано:
- Не доделано:
- Изменённые файлы:
- Заметки для следующего чата:

---

## Этап 1 — сборка и разбор кода
- Сделано:
  - Сборка под Windows 11 через MSYS2 UCRT64 (GCC 16.2, CMake 4.4, Ninja) в `D:\msys64`, без Visual Studio и без прав администратора. Инструкция — `mod-docs/BUILD-WINDOWS.md`.
  - Скрипт `metro_build.ps1`: конфигурация + сборка + скачивание OpenGFX/OpenSFX в `build\baseset`.
  - Полная сборка RelWithDebInfo без ошибок; смоук-тест (генерация карты, 3000 тиков без окна) завершился с кодом 0.
  - Карта кода — `mod-docs/CODEMAP.md`.
- Не доделано:
  - Игра в окне вручную не запускалась (сессия шла без пользователя) — проверить `build\openttd.exe`.
- Изменённые файлы: `metro_build.ps1` (новый), `mod-docs/BUILD-WINDOWS.md` (новый), `mod-docs/CODEMAP.md` (новый), `mod-docs/PROGRESS.md`.
- Заметки для следующего чата:
  - Пересборка: `powershell -ExecutionPolicy Bypass -File metro_build.ps1` (инкрементальная, быстро).
  - Компилятор GCC, а не MSVC: на Windows HTTP идёт через WinHTTP (`src/network/core/http_winhttp.cpp`) — подходит для Gemma.
  - Сейвы JGR — `src/sl/` (фичи через `XSLFI_*` в `src/sl/extended_ver_sl.h`); `src/saveload/` — загрузчик ванильных сейвов, не трогать.
  - Основное зеркало MSYS2 у пользователя тормозит, используется mirror.yandex.ru.
  - `metro_build.ps1` сохранён в UTF-8 с BOM — иначе Windows PowerShell 5.1 ломает кириллицу.

---
