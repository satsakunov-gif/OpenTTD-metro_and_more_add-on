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
- `src/disaster_vehicle.cpp` — спецтранспорт-катастрофы.
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
- `src/infrastructure.cpp` — содержание инфраструктуры (ЛЭП).
- `src/newgrf/`, `src/newgrf.cpp` — загрузка NewGRF (образец табличных данных).
- `src/CMakeLists.txt` — список исходников; новые файлы `metro_*` добавлять сюда (`add_files`).

## Код мода
- `src/metro_ai.h`, `src/metro_ai.cpp` — ИИ-рейтинг администрации и годовой отчёт: конфиг из secrets.cfg, очередь запросов, промпты, разбор ответа, хуки изменений в городах, консольная команда `metro_ai`.
- `src/metro_ai_gui.cpp` — окно «Итоги года».
- `src/sl/metro_ai_sl.cpp` — сейв (чанки MTAI, MTYR).
- `src/table/metro_ai_tables.h` — характеры мэров, веса изменений города.
- `src/metro_city.h`, `src/metro_city.cpp` — активные города (активация по первой перевозке — `MetroCityOnCargoMoved`, хуки в `src/economy.cpp`), службы (команда постройки, пассажиры, рост, больница), туристы, достопримечательности, матчи и анимация стадиона (`MetroCityAdjustHousePalette`, хук в `DrawTile_Town`), хук отрисовки `MetroCityDrawTile`, консоль `metro_city`.
- `src/metro_city_gui.cpp` — окно «Службы» города и окно службы по клику.
- `src/metro_city_cmd.h` — команда `MetroBuildService`.
- `src/sl/metro_city_sl.cpp` — сейв (чанк MTCT; чанки пожаров MTFP, MTFR, MTFT старых сейвов пропускаются).
- `src/table/metro_city_tables.h` — службы, туристы, достопримечательности, уровни матчей, мигание прожекторов стадиона.
- Объекты служб — типы 5–7 в `src/object_type.h` и `src/table/object_land.h`, тип 8 зарезервирован (бывшая пожарная часть, удаляется при загрузке в `afterload.cpp`); сдвиг NewGRF-объектов старых сейвов — `src/sl/object_sl.cpp`, `src/sl/afterload.cpp`.
- `src/metro_power.h`, `src/metro_power.cpp` — электросеть (этап 4): ЛЭП (`_metro_power_lines`, тайл → владелец), состояния станций (`_metro_power_plants`: топливо, выработка), поиск сетей заливкой по касанию (`RebuildNetworks`), раз в 32 тика — мощность, потребление, блэкаут по приоритету и мощность электровозов (`UpdatePower`), ежедневно — топливо и аварийные дизели, ежемесячно — содержание; команды `MetroBuildPowerLine`, `MetroBuildPowerObject`, `MetroSetPowerPriority`; отрисовка ЛЭП `MetroPowerDrawTile` (хук в `ViewportAddLandscape`); консоль `metro_power`.
- `src/metro_power_gui.cpp` — окно «Электросеть» (кнопка «Сеть» на панели путей, `src/rail_gui.cpp`) и окно постройки сети с данными сети (клик по объекту).
- `src/metro_power_cmd.h` — команды сети. `src/sl/metro_power_sl.cpp` — сейв (MTPL, MTPP, MTPC). `src/table/metro_power_tables.h` — постройки, мощности, топливо, цены, радиусы.
- Хуки электричества: тяга электровоза — `MetroPowerGetTrainPowerPercent` в `GroundVehicle::GetAcceleration` (`src/ground_vehicle.cpp`) и `Train::UpdateSpeed` (`src/train_cmd.cpp`); уголь ванильной станции — `TriggerIndustryProduction`, нефть газовой станции — `DeliverGoods` (`src/economy.cpp`); строки в окне индустрии — `src/industry_gui.cpp`; дома не строятся под ЛЭП — `CanBuildHouseHere` (`src/town_cmd.cpp`); объекты сети — типы 9–14 (`src/object_type.h`, `src/table/object_land.h`, сдвиг NewGRF-объектов старых сейвов — `src/sl/object_sl.cpp`, `src/sl/afterload.cpp`); настройка `vehicle.metro_trains_need_grid`.
- `src/metro_town.h`, `src/metro_town.cpp` — жизнь города (этап 5): районы (`_metro_districts`, `GetMetroDistrictTypeAt`), вероятность деловых домов (`MetroTownAdjustHouseProbability`, хук в `TryBuildTownHouse`), поездки на работу (ежедневно), миграция и привлекательность (раз в 5 месяцев), влияние на рост (`MetroTownAdjustGrowthRate`, из `MetroCityAdjustGrowthRate`), строки окна города; консоль `metro_town`. Поездки идут по городам частями каждый тик (`UpdateCommutingSlice`). `src/metro_town_gui.cpp` — окно «Районы». `src/sl/metro_town_sl.cpp` — сейв (MTDS, MTTL). `src/table/metro_town_tables.h` — числа.
- `src/metro_traffic.h`, `src/metro_traffic.cpp` — машинки, пробки и пешеходы (этап 6), всё визуальное и не в сейве: города в видимых окнах (`UpdateTownsInView`, раз в 32 тика), машинки (`_cars`, движение по пикселю без поиска пути — `MoveCar`/`ChooseDirection`), пешеходы (`_pedestrians`), оценка пробок по городу (`UpdateJamEstimates`, каждый город раз в 256 тиков), замедление автобусов игрока `MetroTrafficAdjustRoadVehicleSpeed` (хук в `RoadVehicle::GetCurrentMaxSpeed`), отрисовка `MetroTrafficDrawTile` (хук в `ViewportAddLandscape`), день матча через `MetroCityGetMatchDayStadium`; консоль `metro_traffic status`. `src/table/metro_traffic_tables.h` — числа и спрайты.
- `src/metro_elevated.h`, `src/metro_elevated.cpp`, `src/metro_elevated_cmd.h` — надземный монорельс (этап 7): уровень тайла в битах карты (`GetMetroElevation`, рампа `IsMetroRamp`), высота пути (`MetroElevatedTrackPixelZ`, хуки в `GetSlopePixelZ_Rail`, `GetSlopePixelZ_Station`, `GetSlopePixelZ_TunnelBridge`, `GetBridgeHeight`), проверка стыков по высоте (`MetroCheckTrackEdges`, хуки в `CmdBuildSingleRail`, `CmdBuildRailStation`, `CmdBuildTrainDepot`, `CmdBuildBridge`), отрисовка опор и подъём спрайтов пути как у фундамента (`MetroDrawElevatedBase`, хуки в `DrawTile_Rail`, `DrawTile_Station`, `DrawTile_TunnelBridge`; пролёты над домами — `DrawBridgeMiddle` в `DrawTile_Town`), команды `MetroElevateLine`, `MetroElevateRail`, `MetroBuildElevatedSpan` (обёртка над `CmdBuildBridge` с `_metro_elevated_bridge_level`), окно «Надземный монорельс» (кнопка «Эстакада» на панели путей), консоль `metro_elevated`. `src/table/metro_elevated_tables.h` — числа.
- `src/metro_station_names.h`, `src/metro_station_names.cpp` — названия новых станций по объекту или району (этап 7, хук `MetroNameNewStation` в `BuildStationPart`), консоль `metro_station near`. `src/table/metro_station_names_tables.h` — объекты, радиус, строки.
- `src/metro_trams.h`, `src/metro_trams.cpp` — встроенные трамваи (этап 7): движки в области `METRO_TRAM_GRFID` таблицы номеров движков (`MetroSetupTramEngines` в `AfterLoadGRFs`, пропуск в `FinaliseEngineArray`, запуск в старых сейвах `MetroTramsAfterLoad` → `StartupEngineAddedAfterLoad`), консоль `metro_trams list|track`. `src/table/metro_tram_tables.h` — набор. Обособленный трамвайный путь — `ROADTYPE_METRO_TRAM` в `src/road_type.h` и `src/table/roadtypes.h`; без пробок — `MetroTrafficAdjustRoadVehicleSpeed`.
- `src/metro_underground.h`, `src/metro_underground.cpp`, `src/metro_underground_cmd.h` — метро, этап 8 шаг 1: разреженный подземный слой `_metro_underground` (ключ `MetroUndergroundKey(тайл, слой)` → пути и владелец), даты слоёв `GetMetroLayerYear` (по первым локомотивам ELRL/MONO/MGLV), правила поверхности `MetroCheckUndergroundSurface`, команды `MetroBuildUndergroundTrack`, `MetroBuildEntrance` (объект `OBJECT_METRO_ENTRANCE` = 15), вид «поверхность / подземка» `MetroToggleUndergroundView` / `MetroCycleUndergroundLayer` (Ctrl+U, Ctrl+Shift+U в `main_gui.cpp`) и его отрисовка `MetroUndergroundDrawTile` (хук в `ViewportAddLandscape`), наклоны между слоями (`MetroUndergroundTile::incline`, команда `MetroBuildIncline`), окно «Метро» (кнопка на панели путей), консоль `metro_underground`. `src/sl/metro_underground_sl.cpp` — сейв (MTUG). `src/table/metro_underground_tables.h` — числа.
