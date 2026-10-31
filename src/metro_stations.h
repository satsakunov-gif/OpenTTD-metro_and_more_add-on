/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_stations.h Metro mod: underground stations — levels (escalators, interchange, megahub), rating,
 * entrances on a sidewalk and in a basement, joining through an entrance (stage 8, steps 4 and 7).
 */

#ifndef METRO_STATIONS_H
#define METRO_STATIONS_H

#include "command_type.h"
#include "company_type.h"
#include "station_type.h"
#include "tile_type.h"

#include <functional>
#include <map>

struct Station;
struct TileInfo;
#include "tilearea_type.h"

/** Kind of an entrance that is no pavilion object. */
enum class MetroEntranceKind : uint8_t {
	Sidewalk = 0, ///< On a road tile, the road stays free (-2/-3).
	Basement = 1, ///< In a house of the town (-3).
};

/** An entrance on a road or in a house. */
struct MetroExtraEntrance {
	uint8_t owner = 0;
	MetroEntranceKind kind = MetroEntranceKind::Sidewalk;
};

extern std::map<uint16_t, uint8_t> _metro_station_levels;              ///< Underground station -> level (absent = basic).
extern std::map<uint32_t, MetroExtraEntrance> _metro_extra_entrances;  ///< Tile -> sidewalk or basement entrance.

void MetroStationsReset();
uint8_t GetMetroStationLevel(StationID station);
bool IsMetroEntranceTileOf(TileIndex tile, Owner owner);
void MetroForEachEntranceNear(const TileArea &area, uint radius, Owner owner, const std::function<void(TileIndex)> &f);
int MetroStationRatingModifier(const Station *st);
StationID MetroFindEntranceJoinStation(const TileArea &platforms, Owner owner);
void MetroStationsOnStationDeleted(StationID station);
void MetroStationsDrawTile(const TileInfo *ti);
void MetroStationsRecomputeNear(TileIndex tile);
void MetroStationsConsoleList();

#endif /* METRO_STATIONS_H */
