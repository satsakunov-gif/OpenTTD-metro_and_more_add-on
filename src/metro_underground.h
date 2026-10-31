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
#include "direction_type.h"
#include "core/bitmath_func.hpp"

#include <map>
#include <span>
#include <string_view>

struct TileInfo;
struct BaseStation;

static constexpr uint8_t METRO_UNDERGROUND_PLATFORM = 0xFF; ///< #MetroUndergroundTile::depot of a station platform.

/** One underground tile: track on a layer under a surface tile. */
struct MetroUndergroundTile {
	uint8_t tracks = 0;  ///< #TrackBits.
	uint8_t owner = 0;   ///< #Owner.
	uint8_t incline = 0; ///< Incline to the next deeper layer: bit 7 set, bits 0..1 the direction down, 2..3 the index from the top, 4..5 the length.
	uint8_t depot = 0;   ///< Underground depot on this layer: exit direction + 1, #METRO_UNDERGROUND_PLATFORM for a station platform, or 0.

	bool IsIncline() const { return HasBit(this->incline, 7); }
	DiagDirection GetInclineDown() const { return static_cast<DiagDirection>(GB(this->incline, 0, 2)); }
	uint GetInclineIndex() const { return GB(this->incline, 2, 2); }
	uint GetInclineLength() const { return GB(this->incline, 4, 2); }
	static uint8_t MakeIncline(DiagDirection down, uint index, uint length) { return static_cast<uint8_t>(0x80 | (length << 4) | (index << 2) | to_underlying(down)); }
};

/** Key of an underground tile: surface tile and layer (1..3 for -1..-3). */
inline uint64_t MetroUndergroundKey(TileIndex tile, uint layer)
{
	return (static_cast<uint64_t>(tile.base()) << 2) | layer;
}

extern std::map<uint64_t, MetroUndergroundTile> _metro_underground;
extern bool _metro_underground_view;          ///< The view shows the underground (surface translucent).
extern uint8_t _metro_underground_view_layer; ///< Layer shown bright in the underground view (1..3).

void MetroUndergroundReset();
const MetroUndergroundTile *GetMetroUndergroundTile(TileIndex tile, uint layer);
bool IsMetroLayerAvailable(uint layer);
int32_t GetMetroLayerYear(uint layer);
CommandCost MetroCheckUndergroundSurface(TileIndex tile, uint layer);
bool IsMetroEntranceObject(ObjectType type);
CommandCost MetroCheckSurfaceRail(TileIndex tile, uint8_t bits);
bool MetroStationIsUnderground(const BaseStation *st);

void MetroUndergroundDrawTile(const TileInfo *ti);
void MetroToggleUndergroundView();
void MetroCycleUndergroundLayer();
void ShowMetroUndergroundToolbar();

bool ConMetroUnderground(std::span<std::string_view> argv);

#endif /* METRO_UNDERGROUND_H */
