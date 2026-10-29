/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_city_tables.h Data tables of the metro mod town features: services, fires, tourists and matches. */

#ifndef METRO_CITY_TABLES_H
#define METRO_CITY_TABLES_H

#include "../metro_city.h"
#include "../object_type.h"
#include "sprites.h"
#include "strings.h"

/** Description of a town service. */
struct MetroServiceSpec {
	StringID name;            ///< Name of the service.
	StringID effect;          ///< What the service does, shown in its window.
	ObjectType object;        ///< Object type on the map, #INVALID_OBJECT_TYPE for the road post.
	uint32_t min_population;  ///< Residents the town needs before the service can be built.
	uint8_t cost_factor;      ///< Build cost in 1/256 of the town action base price (statue = 35).
	uint8_t passengers;       ///< Passengers produced and accepted per 256 ticks (pupils, patients, students).
	uint8_t trucks;           ///< Fire engines of the service.
	uint8_t radius;           ///< Protection radius in tiles (Manhattan distance) of a fire service.
};

/** Services, indexed by #MetroServiceType. */
static const MetroServiceSpec _metro_service_specs[] = {
	{ STR_METRO_SERVICE_SCHOOL,       STR_METRO_SERVICE_EFFECT_SCHOOL,       OBJECT_METRO_SCHOOL,       2000, 30,  8, 0,  0 },
	{ STR_METRO_SERVICE_HOSPITAL,     STR_METRO_SERVICE_EFFECT_HOSPITAL,     OBJECT_METRO_HOSPITAL,     3000, 45,  4, 0,  0 },
	{ STR_METRO_SERVICE_UNIVERSITY,   STR_METRO_SERVICE_EFFECT_UNIVERSITY,   OBJECT_METRO_UNIVERSITY,   4000, 70, 12, 0,  0 },
	{ STR_METRO_SERVICE_FIRE_STATION, STR_METRO_SERVICE_EFFECT_FIRE_STATION, OBJECT_METRO_FIRE_STATION,    0, 20,  0, 2, 24 },
	{ STR_METRO_SERVICE_FIRE_POST,    STR_METRO_SERVICE_EFFECT_FIRE_POST,    INVALID_OBJECT_TYPE,          0,  6,  0, 1, 12 },
};
static_assert(std::size(_metro_service_specs) == METRO_SERVICE_COUNT);

/** A fire station may replace a 1x1 house with at most this many residents ("small house"), without a rating penalty. */
static constexpr uint8_t METRO_SMALL_HOUSE_MAX_POPULATION = 30;

/** Town growth: percent of the normal growth interval without a school (pop. 2000+) and with a university. Bigger is slower. */
static constexpr uint METRO_GROWTH_NO_SCHOOL_PERCENT = 150;
static constexpr uint METRO_GROWTH_UNIVERSITY_PERCENT = 80;

/** Monthly town rating change of the owner of a hospital without a road next to it, and the lowest rating it can cause. */
static constexpr int METRO_HOSPITAL_NO_ROAD_RATING = -15;
static constexpr int METRO_HOSPITAL_NO_ROAD_MIN_RATING = -200;

/* Fires. Timers are in game ticks so they do not depend on the day length factor. */
static constexpr uint32_t METRO_FIRE_TIMER_TICKS = 4000;      ///< 120 seconds at 30 ms per tick until the house burns down.
static constexpr uint32_t METRO_FIRE_DAILY_CHANCE = 150;      ///< Each day an active town catches fire with chance 1 / this.
static constexpr uint16_t METRO_FIRE_EXTINGUISH_TICKS = 300;  ///< Ticks the engine needs on site to put the fire out.
static constexpr uint16_t METRO_FIRE_SMOKE_INTERVAL = 24;     ///< Ticks between smoke clouds over a burning house.
static constexpr uint16_t METRO_TRUCK_TICKS_PER_TILE = 12;    ///< Speed of a fire engine (a 100 km/h bus needs ~20 ticks per tile).
static constexpr uint METRO_TRUCK_MAX_SEARCH = 20000;         ///< Maximum road tiles visited when looking for a route.
static constexpr SpriteID METRO_TRUCK_SPRITE = 0xCD4;         ///< Vanilla bus sprite (8 directions), temporary.
static constexpr PaletteID METRO_TRUCK_PALETTE = PALETTE_TO_RED;
static constexpr PaletteID METRO_FIRE_POST_PALETTE = PALETTE_TO_RED;

/* Tourists. */
static constexpr uint METRO_TOURIST_SHARE_PERCENT = 10;        ///< Share of passengers to a station near a landmark who are tourists.
static constexpr uint METRO_TOURIST_SHARE_SUMMER_PERCENT = 20; ///< The same in summer (June to August).
static constexpr uint METRO_TOURIST_EXTRA_PAY_PERCENT = 100;   ///< Tourists pay this much more than other passengers.

/** Original houses that are landmarks (besides churches and stadiums, which are found by their flags). */
static const StringID _metro_landmark_house_names[] = {
	STR_TOWN_BUILDING_NAME_STATUE_1,
	STR_TOWN_BUILDING_NAME_FOUNTAIN_1,
	STR_TOWN_BUILDING_NAME_PARK_1,
	STR_TOWN_BUILDING_NAME_THEATER_1,
	STR_TOWN_BUILDING_NAME_CINEMA_1,
};

/* Stadium matches. */
static constexpr uint METRO_MATCH_MIN_INTERVAL_DAYS = 60;   ///< Shortest gap between two matches of a town.
static constexpr uint METRO_MATCH_MAX_INTERVAL_DAYS = 240;  ///< Longest gap between two matches of a town.
static constexpr uint METRO_MATCH_ANNOUNCE_DAYS = 30;       ///< The news comes this many days before the match.
static constexpr uint METRO_MATCH_REGION_TILES = 100;       ///< Neighbour towns of a regional match are within this distance.
static constexpr uint METRO_MATCH_MAX_FAN_TOWNS = 5;        ///< Fan towns named in the news.

/** Description of a match importance level. */
struct MetroMatchSpec {
	StringID name;          ///< Name in the news.
	uint8_t weight;         ///< Relative chance of the level.
	uint16_t bonus_percent; ///< Extra income for passengers delivered near the stadium on match day.
	uint16_t fans;          ///< Fans leaving from the stadium after the match.
};

/** Match levels, indexed by #MetroMatchLevel. */
static const MetroMatchSpec _metro_match_specs[] = {
	{ STR_METRO_CITY_MATCH_LOCAL,    60,  50,  40 },
	{ STR_METRO_CITY_MATCH_REGIONAL, 30, 100, 100 },
	{ STR_METRO_CITY_MATCH_FINAL,    10, 200, 250 },
};
static_assert(std::size(_metro_match_specs) == to_underlying(MetroMatchLevel::End));

#endif /* METRO_CITY_TABLES_H */
