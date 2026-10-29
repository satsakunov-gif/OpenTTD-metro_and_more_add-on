# Карта кода

Короткая карта форка (JGR's Patchpack) — где что лежит. Все пути от корня репозитория.

## Тайлы карты
- `src/map_type.h`, `src/map_func.h`, `src/map.cpp` — хранилище карты (`Tile`, `TileExtended`, поля m1..m8), размеры и обход тайлов.
- `src/tile_type.h`, `src/tile_map.h` — типы тайлов (`TileType`), высота, владелец.
- `src/tile_cmd.h`, `src/landscape.cpp` — таблица обработчиков типа тайла (`_tile_type_procs`: отрисовка, снос, тики) и общий цикл ландшафта.
- `docs/landscape.html`, `docs/landscape_grid.html` — раскладка битов m1..m8 для каждого типа тайла.
- `src/rail_map.h`, `src/rail_cmd.cpp` — тайлы путей: биты карты и команды постройки.
- `src/tunnel_map.h`, `src/tunnelbridge_map.h`, `src/tunnelbridge_cmd.cpp` — тоннели и мосты (основа для метро).
- `src/road_map.h`, `src/road_cmd.cpp` — дороги и трамвайные пути.
- `src/station_map.h`, `src/station_cmd.cpp` — тайлы и постройка станций, зона охвата.
- `src/object_map.h`, `src/object_cmd.cpp` — объекты (маяки, ЛЭП ванили, своё — вентшахты, подстанции).
- `src/clear_cmd.cpp`, `src/tree_cmd.cpp`, `src/water_cmd.cpp`, `src/industry_cmd.cpp` — пустая земля, деревья, вода, индустрии.
- `src/viewport.cpp` — отрисовка карты в окне (слой «подземка» придётся встраивать сюда).
- `src/smallmap_gui.cpp` — мини-карта (режимы карты, тепловые карты).

## Транспорт
- `src/vehicle_base.h`, `src/vehicle.cpp` — базовый класс транспорта, общий тик.
- `src/train.h`, `src/train_cmd.cpp` — поезда (движение, ускорение, питание).
- `src/roadveh.h`, `src/roadveh_cmd.cpp` — автобусы, грузовики, трамваи.
- `src/ship_cmd.cpp`, `src/aircraft_cmd.cpp` — корабли и самолёты.
- `src/effectvehicle.cpp`, `src/effectvehicle_base.h` — визуальные эффекты (образец для пешеходов).
- `src/disaster_vehicle.cpp` — спецтранспорт-катастрофы (образец для пожарных/преступников).
- `src/engine.cpp`, `src/table/engines.h` — модели транспорта и их таблица.
- `src/rail.h`, `src/table/railtypes.h` — типы путей (обычные, электро, монорельс, маглев).
- `src/elrail.cpp` — электрификация путей и провода.
- `src/depot_cmd.cpp`, `src/depot_gui.cpp` — депо.
- `src/order_cmd.cpp`, `src/timetable_cmd.cpp` — приказы и расписания.
- `src/pathfinder/yapf/` — поиск пути (YAPF).
- `src/linkgraph/` — граф связей и распределение пассажиров/грузов.
- `src/economy.cpp` — погрузка/выгрузка, доходы, ежемесячная экономика.
- `src/station.cpp`, `src/station_base.h` — станции, рейтинг станций.

## Города
- `src/town.h` — структура `Town` (население, рост, `ratings` компаний).
- `src/town_cmd.cpp` — тик города (`OnTick_Town`), рост, постройка домов, годовой цикл (`TownsYearlyLoop`).
- `src/house.h`, `src/table/town_land.h` — описания домов и их спрайты.
- `src/newgrf_house.cpp`, `src/newgrf_town.cpp` — NewGRF-дома и переменные города.
- `src/town_map.h` — биты тайла дома.
- `src/subsidy.cpp`, `src/goal.cpp`, `src/story.cpp` — субсидии, цели и сюжетные страницы (пригодится для нужд/контрактов).

## Рейтинг администрации
- `src/town_cmd.cpp` — `UpdateTownRating` (ежемесячный расчёт), `ChangeTownRating` (изменение при сносе/вырубке), `CheckIfAuthorityAllowsNewStation`, действия администрации (реклама, подкуп, финансирование).
- `src/town_gui.cpp` — `TownAuthorityWindow` (окно администрации), `TownViewWindow` (окно города), `TownDirectoryWindow` (список).
- `src/table/settings/difficulty_settings.ini` — настройки отношения городов (`town_council_tolerance`).

## Окна интерфейса
- `src/window.cpp`, `src/window_gui.h` — базовый класс `Window`, `WindowDesc`.
- `src/widget.cpp`, `src/widget_type.h`, `src/widgets/*_widget.h` — виджеты и номера виджетов окон.
- `src/toolbar_gui.cpp`, `src/main_gui.cpp` — главная панель и горячие клавиши.
- `src/news_gui.cpp` — новости (`AddNewsItem`).
- `src/company_gui.cpp` — финансы компании (годовая сводка — `ShowCompanyFinances` из `CompaniesYearlyLoop` в `src/company_cmd.cpp`).
- `src/settings_gui.cpp` — окно настроек.
- `src/lang/english.txt`, `src/lang/russian.txt` — строки интерфейса (новые строки сюда).

## Сохранения
- `src/sl/` — система сохранений JGR (используется для своих сейвов).
- `src/sl/saveload.cpp`, `src/sl/saveload.h` — ядро, `SaveLoad`-описания полей, чанки.
- `src/sl/extended_ver_sl.h`, `src/sl/extended_ver_sl.cpp` — расширенные версии фич (`XSLFI_*`) — сюда добавлять версии фич мода.
- `src/sl/afterload.cpp` — пост-обработка после загрузки, миграции старых сейвов.
- `src/sl/town_sl.cpp`, `src/sl/map_sl.cpp`, `src/sl/station_sl.cpp`, `src/sl/vehicle_sl.cpp` — сохранение городов, карты, станций, транспорта.
- `src/saveload/` — загрузчик сейвов ванильного OpenTTD (`namespace upstream_sl`), не трогать.

## Прочее, нужное на следующих этапах
- `src/date.cpp` — дневной/месячный/годовой циклы (`CompaniesYearlyLoop`, `TownsYearlyLoop`).
- `src/timer/` — таймеры игровых тиков и календаря.
- `src/network/core/http.h`, `src/network/core/http_winhttp.cpp` — HTTP-запросы в фоне (WinHTTP на Windows; для Gemma).
- `src/settings.cpp`, `src/table/settings/*.ini` — настройки; `secrets.cfg` читается в `settings.cpp` (`_secrets_file`), секции в `network_secrets_settings.ini`.
- `src/infrastructure.cpp` — содержание инфраструктуры (ЛЭП, склады).
- `src/newgrf/`, `src/newgrf.cpp` — загрузка NewGRF (образец табличных данных).
- `src/CMakeLists.txt` — список исходников; новые файлы `metro_*` добавлять сюда (`add_files`).

## Код мода
- `src/metro_ai.h`, `src/metro_ai.cpp` — ИИ-рейтинг администрации и годовой отчёт: конфиг из secrets.cfg, очередь запросов, промпты, разбор ответа, хуки изменений в городах, консольная команда `metro_ai`.
- `src/metro_ai_gui.cpp` — окно «Итоги года».
- `src/sl/metro_ai_sl.cpp` — сейв (чанки MTAI, MTYR).
- `src/table/metro_ai_tables.h` — характеры мэров, веса изменений города.
