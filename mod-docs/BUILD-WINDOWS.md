# Сборка под Windows 11 (MSYS2, без Visual Studio)

Без прав администратора, всё ставится в `D:\msys64`.

## Один раз: установка инструментов

1. Скачать `msys2-base-x86_64-latest.sfx.exe` с https://github.com/msys2/msys2-installer/releases и распаковать в `D:\`:
   ```powershell
   .\msys2-base-x86_64-latest.sfx.exe -y -oD:\
   ```
2. Если основное зеркало медленное — первой строкой в `D:\msys64\etc\pacman.d\mirrorlist.msys` и `mirrorlist.mingw` поставить
   `Server = https://mirror.yandex.ru/mirrors/msys2/msys/$arch/` и `Server = https://mirror.yandex.ru/mirrors/msys2/mingw/$repo/` соответственно.
3. Обновить и поставить пакеты:
   ```powershell
   $env:MSYSTEM='UCRT64'
   D:\msys64\usr\bin\bash.exe -lc "pacman -Syuu --noconfirm"
   D:\msys64\usr\bin\bash.exe -lc "pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,zlib,xz,libpng,lzo2,zstd,opusfile,libsoxr}"
   ```

## Сборка

Из корня репозитория:
```powershell
powershell -ExecutionPolicy Bypass -File metro_build.ps1            # RelWithDebInfo
powershell -ExecutionPolicy Bypass -File metro_build.ps1 -Config Debug -Clean
```
Скрипт собирает `build\openttd.exe` и при первом запуске скачивает OpenGFX/OpenSFX в `build\baseset`.
Библиотеки линкуются статически, exe зависит только от системных DLL Windows (на случай динамических DLL скрипт копирует их из MSYS2 рядом с exe).
Повторная сборка инкрементальная (только изменённые файлы). Полная сборка — ~1000 файлов, около 15 минут.
Exe в RelWithDebInfo весит ~800 МБ из-за отладочной информации — это нормально.

Запуск: `build\openttd.exe`.

Быстрая проверка без окна (сгенерировать карту, прогнать 3000 тиков и выйти):
```powershell
build\openttd.exe -v null:ticks=3000 -s null -m null -g
```

## Отладка
Отладчик — `gdb` (`pacman -S mingw-w64-ucrt-x86_64-gdb`), сборка `-Config Debug`. В VS Code — расширение C/C++ с `miDebuggerPath = D:\msys64\ucrt64\bin\gdb.exe`.
