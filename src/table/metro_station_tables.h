/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_station_tables.h Data tables of the underground stations: levels, rating, entrances (stage 8, steps 4 and 7). */

#ifndef METRO_STATION_TABLES_H
#define METRO_STATION_TABLES_H

#include "strings.h"

/** A level of an underground station (DESIGN.md "Станции"); each upgrade is bought with a button. */
struct MetroStationLevelSpec {
	int32_t year;          ///< First year the level can be bought.
	uint8_t cost_factor;   ///< Price: this many times the price of the platforms of the station.
	StringID name;
};

static const MetroStationLevelSpec _metro_station_levels_spec[] = {
	{ 1960,  0, STR_METRO_STATION_LEVEL_BASIC },
	{ 1975,  2, STR_METRO_STATION_LEVEL_ESCALATORS },
	{ 1990,  4, STR_METRO_STATION_LEVEL_INTERCHANGE },
	{ 2030, 10, STR_METRO_STATION_LEVEL_MEGAHUB },
};

static constexpr uint8_t METRO_STATION_LEVEL_ESCALATORS = 1;
static constexpr uint8_t METRO_STATION_LEVEL_INTERCHANGE = 2;
static constexpr uint8_t METRO_STATION_LEVEL_MEGAHUB = 3;

/* Rating (in percent of the full rating). */
static constexpr int METRO_RATING_NO_ESCALATOR_PERCENT = 20; ///< Platforms on -2/-3 without escalators.
static constexpr int METRO_RATING_STUFFY_PERCENT = 15;       ///< Platforms on -3 without a working vent shaft near ("душно").
static constexpr int METRO_RATING_ENTRANCE_PERCENT = 3;      ///< Each entrance after the first.
static constexpr int METRO_RATING_ENTRANCE_MAX_PERCENT = 12; ///< Cap of the entrance bonus.

static constexpr uint METRO_VENT_RADIUS = 8;          ///< A vent shaft airs platforms of -3 within this many tiles (DESIGN.md "Вентиляция").
static constexpr uint METRO_INTERCHANGE_RADIUS = 4;   ///< Another underground station this near makes an interchange possible.

/* Entrances on a road and in a house (DESIGN.md "Вход"). */
static constexpr uint METRO_SIDEWALK_MIN_LAYER = 2;   ///< A sidewalk entrance leads to platforms of -2 or -3.
static constexpr uint METRO_BASEMENT_LAYER = 3;       ///< A basement entrance leads to platforms of -3.
static constexpr uint METRO_BASEMENT_COST_MULTIPLIER = 5; ///< A basement entrance costs this many pavilions.

#endif /* METRO_STATION_TABLES_H */
