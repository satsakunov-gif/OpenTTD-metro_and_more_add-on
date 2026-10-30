/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_city_tables.h Data tables of the metro mod town features: services, tourists and matches. */

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
	ObjectType object;        ///< Object type on the map.
	uint32_t min_population;  ///< Residents the town needs before the service can be built.
	uint8_t cost_factor;      ///< Build cost in 1/256 of the town action base price (statue = 35).
	uint8_t passengers;       ///< Passengers produced and accepted per 256 ticks (pupils, patients, students).
};

/** Services, indexed by #MetroServiceType. */
static const MetroServiceSpec _metro_service_specs[] = {
	{ STR_METRO_SERVICE_SCHOOL,     STR_METRO_SERVICE_EFFECT_SCHOOL,     OBJECT_METRO_SCHOOL,     2000, 30,  8 },
	{ STR_METRO_SERVICE_HOSPITAL,   STR_METRO_SERVICE_EFFECT_HOSPITAL,   OBJECT_METRO_HOSPITAL,   3000, 45,  4 },
	{ STR_METRO_SERVICE_UNIVERSITY, STR_METRO_SERVICE_EFFECT_UNIVERSITY, OBJECT_METRO_UNIVERSITY, 4000, 70, 12 },
};
static_assert(std::size(_metro_service_specs) == METRO_SERVICE_COUNT);

/** Town growth: percent of the normal growth interval without a school (pop. 2000+) and with a university. Bigger is slower. */
static constexpr uint METRO_GROWTH_NO_SCHOOL_PERCENT = 150;
static constexpr uint METRO_GROWTH_UNIVERSITY_PERCENT = 80;

/** Monthly town rating change of the owner of a hospital without a road next to it, and the lowest rating it can cause. */
static constexpr int METRO_HOSPITAL_NO_ROAD_RATING = -15;
static constexpr int METRO_HOSPITAL_NO_ROAD_MIN_RATING = -200;

/* Tourists. */
static constexpr uint METRO_TOURIST_SHARE_PERCENT = 10;        ///< Share of passengers to a station near a landmark who are tourists.
static constexpr uint METRO_TOURIST_SHARE_SUMMER_PERCENT = 20; ///< The same in summer (June to August).
static constexpr uint METRO_TOURIST_EXTRA_PAY_PERCENT = 100;   ///< Tourists pay this much more than other passengers.

/** Original houses that are landmarks (churches and stadiums are also found by their flags). */
static const StringID _metro_landmark_house_names[] = {
	STR_TOWN_BUILDING_NAME_CHURCH_1,
	STR_TOWN_BUILDING_NAME_STADIUM_1,
	STR_TOWN_BUILDING_NAME_STADIUM_2,
	STR_TOWN_BUILDING_NAME_STATUE_1,
	STR_TOWN_BUILDING_NAME_FOUNTAIN_1,
	STR_TOWN_BUILDING_NAME_PARK_1,
	STR_TOWN_BUILDING_NAME_THEATER_1,
	STR_TOWN_BUILDING_NAME_CINEMA_1,
};

/* Stadium matches. */
static constexpr uint METRO_MATCH_MIN_INTERVAL_DAYS = 365;  ///< Shortest gap between two matches at a stadium (matches are rare: once in 1..2 years).
static constexpr uint METRO_MATCH_MAX_INTERVAL_DAYS = 730;  ///< Longest gap between two matches at a stadium.
static constexpr uint METRO_MATCH_ANNOUNCE_DAYS = 30;       ///< The news comes this many days before the match.
static constexpr uint METRO_MATCH_REGION_TILES = 100;       ///< Neighbour towns of a regional match are within this distance.
static constexpr uint METRO_MATCH_MAX_FAN_TOWNS = 5;        ///< Fan towns named in the news.

/** Description of a match importance level. */
struct MetroMatchSpec {
	StringID name;          ///< Name in the news.
	uint8_t weight;         ///< Relative chance of the level.
	uint16_t bonus_percent; ///< Extra income for passengers delivered near the stadium on match day.
	uint16_t fans;          ///< Fans leaving from the stadium after the match.
	uint8_t spark_interval; ///< Ticks between sparks at the floodlights on match day, 0 for none.
	uint8_t flash_interval; ///< Ticks between flashes (small explosions) over the stand on match day, 0 for none.
};

/** Match levels, indexed by #MetroMatchLevel. */
static const MetroMatchSpec _metro_match_specs[] = {
	{ STR_METRO_CITY_MATCH_LOCAL,    70,  50,  40, 24,  0 },
	{ STR_METRO_CITY_MATCH_REGIONAL, 25, 100, 100, 16, 64 },
	{ STR_METRO_CITY_MATCH_FINAL,     5, 200, 250,  8, 24 },
};
static_assert(std::size(_metro_match_specs) == to_underlying(MetroMatchLevel::End));

/*
 * Stadium on match day: the floodlights flicker by a palette brightness adjustment of the stadium sprites
 * (seen with a 32 bpp blitter; the 8 bpp blitters ignore it) and vanilla sparks and flashes go off.
 * Only while the stadium is on screen; cosmetic, so the interactive random is used.
 */
static constexpr uint8_t METRO_STADIUM_LIGHT_FRAME_TICKS = 4; ///< Ticks per step of the floodlight flicker.
static constexpr int8_t _metro_stadium_light_brightness[] = { 0, 6, 12, 8, 3, 12, 0, 5, 10, 4 }; ///< Brightness steps of the flicker (-16..15).
static constexpr int METRO_STADIUM_LIGHT_HEIGHT = 40;         ///< Height of the floodlights above the ground for the sparks, in pixels.

#endif /* METRO_CITY_TABLES_H */
