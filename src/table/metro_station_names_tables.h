/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_station_names_tables.h Data tables of the metro mod station names. */

#ifndef METRO_STATION_NAMES_TABLES_H
#define METRO_STATION_NAMES_TABLES_H

#include "strings.h"

/** Objects a new station is named after, in order of priority when equally near. */
enum class MetroNamedObject : uint8_t {
	Stadium,    ///< Town stadium (house).
	University, ///< Service of the metro mod.
	Hospital,   ///< Service of the metro mod.
	TownHall,   ///< Town centre.
	End,
};

/** Name of the station after each object (town name as parameter). */
static const StringID _metro_named_object_names[] = {
	STR_METRO_STNAME_STADIUM,
	STR_METRO_STNAME_UNIVERSITY,
	STR_METRO_STNAME_HOSPITAL,
	STR_METRO_STNAME_TOWN_HALL,
};
static_assert(std::size(_metro_named_object_names) == static_cast<size_t>(MetroNamedObject::End));

static constexpr uint METRO_STATION_NAME_OBJECT_RADIUS = 4; ///< Objects within this many tiles name the station.

/** Name after the district type (#MetroDistrictType: business, residential). */
static const StringID _metro_district_station_names[] = {
	STR_METRO_STNAME_BUSINESS,
	STR_METRO_STNAME_RESIDENTIAL,
};

/** Clarification by the side of the town centre (#DiagDirection order NE, SE, SW, NW) when the name is taken. */
static const StringID _metro_station_name_sides[] = {
	STR_METRO_STNAME_SIDE_NE,
	STR_METRO_STNAME_SIDE_SE,
	STR_METRO_STNAME_SIDE_SW,
	STR_METRO_STNAME_SIDE_NW,
};

#endif /* METRO_STATION_NAMES_TABLES_H */
