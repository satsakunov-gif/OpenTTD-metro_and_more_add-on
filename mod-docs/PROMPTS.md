# Промпты по этапам

Проект: форк OpenTTD на основе JGR's Patchpack, только одиночная игра.
Репозиторий: https://github.com/satsakunov-gif/OpenTTD-metro_and_more_add-on

## Общие правила для любого этапа

1. Перед работой прочитай `mod-docs/DESIGN.md` (дизайн всех механик), `mod-docs/PROGRESS.md` (что сделано в прошлых этапах) и `mod-docs/CODEMAP.md` (карта кода, появится после этапа 1).
2. Показывай только диффы, не файлы целиком. Не трогай то, что не относится к задаче.
3. Данные (составы, здания, электростанции) выноси в таблицы, а не зашивай в код.
4. Если нужного решения нет в `mod-docs/DESIGN.md` — коротко спроси, не придумывай.
5. Каждый шаг должен компилироваться и работать.
6. В конце этапа допиши в `mod-docs/PROGRESS.md` запись по шаблону (что сделано, что не доделано, какие файлы менялись, заметки для следующего этапа) и сделай коммит.
7. Новые файлы с кодом мода клади в отдельные файлы с префиксом `metro_` или в отдельные папки внутри `src/`, чтобы их было легко отличить от кода JGR.

---

## Этап 1 — сборка и разбор кода

Помоги собрать игру под мою систему (ОС спроси у меня). Потом составь короткую карту кода: где тайлы карты, транспорт, города, рейтинг администрации, окна интерфейса, сохранения — только пути к файлам и 1 строка на каждый. Запиши карту в `mod-docs/CODEMAP.md`.

## Этап 2 — Gemma

Раздел «Gemma» в `mod-docs/DESIGN.md`. По шагам:
1. Ключ Gemma API в `secrets.cfg`.
2. HTTP-запрос в отдельном потоке, игра не подвисает.
3. Сбор данных по городу в промпт.
4. Разбор JSON `{rating, verdict, explanation, needs}`.
5. Рейтинг в шкалу −1000..1000, не больше ±200 за раз, кривой ответ — старая оценка.
6. Текст в окне администрации, нужды как задания.
7. Сохранение в сейв.
8. Без интернета или ключа — ванильная формула.
9. Годовой отчёт вместе с годовой сводкой экономики.

## Этап 3 — активный город, службы, пожары, туристы, стадион

Раздел «Город» в `mod-docs/DESIGN.md`.
1. Механика «активный город» (раздел «Город»), затем службы (школа, больница, университет, пожарная часть) — ставит игрок, окно по клику как у банка. Сначала одно здание полностью, остальные по шаблону.
2. Пожарная часть вместо маленького дома (замена), пожарные мини-посты drive-through на дороге со своими машинами.
3. Пожары (120 секунд, таймер в тиках). Всё это — только в активных городах.
4. Туристы и зелёная рамка достопримечательностей по кнопке, свои достопримечательности.
5. Матчи на стадионе с новостями и важностью.

## Этап 4 — электричество

Раздел «Электричество» в `mod-docs/DESIGN.md`. Начни со структуры данных сети и сохранения в сейв. Затем: электростанции, ЛЭП как новая инфраструктура, подстанции с радиусом 8, электровозы только с питанием, потребители, блэкаут по приоритетам, аварийные генераторы, окно сети.

## Этап 5 — живой город

Раздел «Город» в `mod-docs/DESIGN.md`. По одной механике за шаг: районы (деловой и спальный) → поездки на работу → миграция → забастовки → погода и снегоуборщики.

## Этап 6 — машинки и пешеходы

Разделы «Город» (машинки) и «Людишки и визуал» (пешеходы). Машинки — упрощённая логика без поиска пути, пробки замедляют автобусы, число зависит от покрытия транспортом. Пешеходы — визуальные объекты по образцу EffectVehicle, не в сейве, только при сильном зуме. Следи за производительностью.

## Этап 7 — транспорт

Раздел «Транспорт». По одной механике: надземный монорельс → названия станций → трамваи (свой набор, обособленные пути, преметро через трансформер).

## Этап 8 — метро

Раздел «Метро». Глубина — свойство тайла пути, без отдельного массива карты. По шагам:
1. Вход на 1 тайл и пути на слоях −1..−3.
2. Вид «поверхность / подземка».
3. Наклонные переходы и пересечения.
4. Станции (объединение касанием, радиус 3, тротуар и подвал).
5. Депо и трансформер.
6. Составы и вагоны из таблицы.
7. Вентшахты на −3 с питанием от сети.

## Этап 9 — остальное (один чат на одну механику)

Выбери механику, которую я назову: подземные ТЦ, режимы карты (тепловые карты), кинокамера, гос-контракты, выборы мэра, режим «Диспетчер». Всё по `mod-docs/DESIGN.md`.

---

# Спрайты через ChatGPT

Каждый промпт ниже — для одного спрайта, вставляй в ChatGPT по одному. Перед каждым вставляй общий стиль.

**Общий стиль (вставлять первым):**

```text
Pixel art sprite for the game OpenTTD (Transport Tycoon style). Isometric view, 2:1 ratio, camera from the south-east. Clean pixel art, no anti-aliasing, no text unless asked, plain single-color magenta (#FF00FF) background. Style matches classic OpenTTD graphics. Object fits exactly one tile unless stated otherwise.
```

**Как доводить:** уменьшить до размера тайла (около 64×31 px при стандартном зуме, сверь с вики OpenTTD), убрать фон, выровнять изометрию в Aseprite или GIMP, сохранить как 32bpp PNG. Транспорт — 8 направлений на одном листе (sprite sheet).

## Метро

```text
Metro entrance pavilion, 1960s style: small concrete pavilion with stairs going down and a red "M" sign.
```

```text
Metro entrance pavilion, modern 2000s style: glass canopy over an escalator going down, red "M" sign.
```

```text
Metro entrance on a sidewalk: narrow stairway opening in the pavement with railings and a red "M" sign post, no building.
```

```text
Metro entrance built into the ground floor of a city building: doorway with a red "M" sign above it.
```

```text
Metro ventilation shaft: small square kiosk with metal grilles on the sides, one tile.
```

```text
Underground metro tunnel track segment seen from above with the ground cut away: two rails in a concrete tunnel, straight piece.
```

```text
Sloped metro tunnel portal: track going down into a concrete tunnel mouth, transition from surface to underground.
```

```text
Underground metro platform, cut-away view: track with a side platform, tiled walls and lamps.
```

```text
Underground metro depot, cut-away view: concrete hall with a track entering a large gate.
```

```text
Rail "transformer" tile: short track section with a technical gate, warning stripes and equipment boxes, where metro track meets a normal railway.
```

```text
Metro train head car "Series A", 1960s: blue and light-grey rounded car, small windows. 8 isometric directions, sprite sheet.
```

```text
Metro train head car "Series E", 1970s: blue and cream car with flat front, longer. 8 isometric directions, sprite sheet.
```

```text
Premetro tram-train, 1980s: red and white articulated light rail car. 8 isometric directions, sprite sheet.
```

```text
Metro train "Series 2000": modern white car with blue stripe and wide windows. 8 isometric directions, sprite sheet.
```

```text
Fast intercity metro train "Diameter", 2020s: sleek silver car with red accents. 8 isometric directions, sprite sheet.
```

```text
Deep metro train "Deep line": heavy dark-grey car with yellow safety stripes. 8 isometric directions, sprite sheet.
```

```text
Vacuum metro train, 2040s: futuristic white aerodynamic capsule car. 8 isometric directions, sprite sheet.
```

```text
Metro "Capsule" pod, 2060s: small autonomous glossy pod vehicle with glowing lights. 8 isometric directions, sprite sheet.
```

```text
Metro freight wagons set in the same style as a metro car: open wagon for coal, covered wagon for goods, mail wagon, tank wagon. 8 isometric directions each, sprite sheet.
```

## Электричество

```text
Gas power plant, 1970s: building with two slim chimneys and pipes, 2x2 tiles.
```

```text
Nuclear power plant: two cooling towers and a reactor dome, 3x3 tiles.
```

```text
Solar power plant: field of solar panels in rows, 2x2 tiles.
```

```text
Wind turbine: tall white three-blade wind turbine, one tile, 4 animation frames of rotating blades.
```

```text
High-voltage power line pylon: steel lattice tower with wires, one tile, straight and corner variants.
```

```text
Electrical substation: small fenced area with transformers and insulators, one tile.
```

```text
Emergency diesel generator: small metal container with exhaust pipe, one tile.
```

## Транспорт

```text
Elevated monorail track on a concrete pillar, high above the ground, straight piece.
```

```text
Elevated monorail track segment without a pillar, high above the ground, straight piece.
```

```text
Elevated monorail station: platform on pillars high above the street with a staircase down to a small entrance.
```

```text
Freight warehouse, small: single brick storage building with a loading door, one tile.
```

```text
Freight warehouse, large: long hangar with several loading doors, 2x2 tiles.
```

```text
Snowplow truck: orange truck with a front plow blade. 8 isometric directions, sprite sheet.
```

```text
Tram, 1950s: small red and cream tram car. 8 isometric directions, sprite sheet.
```

```text
Tram, 1970s: articulated yellow and red tram. 8 isometric directions, sprite sheet.
```

```text
Tram, 2000s: modern low-floor white and green articulated tram. 8 isometric directions, sprite sheet.
```

```text
Tram, 2040s: futuristic silver tram with glass sides. 8 isometric directions, sprite sheet.
```

```text
Separated tram track: tram rails on a green grass lane with small curbs, next to a road, straight piece.
```

## Город

```text
Private cars, 1950s: set of 4 small cars in different colors. 8 isometric directions each, sprite sheet.
```

```text
Private cars, 1970s: set of 4 boxy cars in different colors. 8 isometric directions each, sprite sheet.
```

```text
Private cars, 2000s: set of 4 modern hatchbacks and sedans. 8 isometric directions each, sprite sheet.
```

```text
Private cars, future: set of 4 futuristic compact electric cars. 8 isometric directions each, sprite sheet.
```

```text
School building: two-storey brick school with a schoolyard and flag, 2x2 tiles.
```

```text
Hospital: white multi-storey building with a red cross and ambulance bay, 2x2 tiles.
```

```text
University: large classical building with columns, 3x3 tiles.
```

```text
Fire station: red brick building with two garage doors, one tile.
```

```text
Fire mini-post on a road: small drive-through booth across the road with red stripes and a siren light.
```

```text
Fire truck: red fire engine with ladder and flashing lights. 8 isometric directions, 2 frames of flashing lights, sprite sheet.
```

```text
House on fire overlay: flames and smoke, 4 animation frames, transparent background, to place over a building.
```

```text
Museum landmark: classical museum with dome and steps, 2x2 tiles.
```

```text
Tower landmark: tall observation tower with antenna, one tile.
```

```text
Park landmark: small park with fountain, trees and benches, 2x2 tiles.
```

## Люди и погода

```text
Pedestrians, 1950s clothing: tiny walking person about 8 pixels tall, 4 directions, 4 walking frames each, 4 color variants, sprite sheet.
```

```text
Pedestrians, 1970s clothing: tiny walking person about 8 pixels tall, 4 directions, 4 walking frames each, 4 color variants, sprite sheet.
```

```text
Pedestrians, 2000s clothing: tiny walking person about 8 pixels tall, 4 directions, 4 walking frames each, 4 color variants, sprite sheet.
```

```text
Pedestrians, futuristic clothing: tiny walking person about 8 pixels tall, 4 directions, 4 walking frames each, 4 color variants, sprite sheet.
```

```text
Pedestrians in winter: tiny walking person in coat and hat, 4 directions, 4 walking frames, sprite sheet.
```

```text
Pedestrians with umbrellas: tiny walking person holding an umbrella, 4 directions, 4 walking frames, sprite sheet.
```

```text
Protesters: tiny standing people holding signs, 4 variants, 2 frames of waving signs, sprite sheet.
```

```text
Rain overlay: diagonal raindrops, 4 animation frames, transparent background, tileable.
```

```text
Snowfall overlay: falling snowflakes, 4 animation frames, transparent background, tileable.
```

```text
Winter grass tile: snow-covered flat grass ground tile, plus sloped variants.
```
