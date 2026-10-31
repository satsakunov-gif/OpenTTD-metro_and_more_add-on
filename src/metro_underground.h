/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_underground.h Metro mod: the underground layers -1..-3.
 *
 * The underground is a separate sparse layer: only underground tiles are stored (surface tile + depth -> track and
 * owner), not a full map array, so the metro can lie under roads and houses. The depth is part of the underground
 * tile record. Entrances are one-tile objects on the surface.
 */

#ifndef METRO_UNDERGROUND_H
#define METRO_UNDERGROUND_H

#include "command_type.h"
#include "company_type.h"
#include "object_type.h"
#include "tile_type.h"
#include "track_type.h"

#include <map>
#include <span>
#include <string_view>

struct TileInfo;

/** One underground tile: track on a layer under a surface tile. */
struct MetroUndergroundTile {
	uint8_t tracks = 0;  ///< #TrackBits.
	uint8_t owner = 0;   ///< #Owner.
};

/** Key of an underground tile: surface tile and layer (1..3 for -1..-3). */
inline uint64_t MetroUndergroundKey(TileIndex tile, uint layer)
{
	return (static_cast<uint64_t>(tile.base()) << 2) | layer;
}

extern std::map<uint64_t, MetroUndergroundTile> _metro_underground;
extern uint8_t _metro_underground_view_layer; ///< Layer shown in the view (0: surface); full view in step 2.

void MetroUndergroundReset();
const MetroUndergroundTile *GetMetroUndergroundTile(TileIndex tile, uint layer);
bool IsMetroLayerAvailable(uint layer);
int32_t GetMetroLayerYear(uint layer);
CommandCost MetroCheckUndergroundSurface(TileIndex tile, uint layer);
bool IsMetroEntranceObject(ObjectType type);

void MetroUndergroundDrawTile(const TileInfo *ti);
void ShowMetroUndergroundToolbar();

bool ConMetroUnderground(std::span<std::string_view> argv);

#endif /* METRO_UNDERGROUND_H */
