/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_overlay.h Metro mod: the underground as full map tiles (stage 8, steps 4-5 foundation).
 *
 * Every underground tile is kept as a complete map tile (rail track, depot, later stations): #_metro_overlay maps
 * the surface tile index to its tile data. While the engine works on the underground - the trains tick, a command
 * builds a depot or a train under the ground - #MetroUndergroundScope swaps all these tiles into the map in place of
 * the surface (the height stays the surface one) and back afterwards. So the vanilla code for trains, signals, path
 * finding and depots runs unchanged on the underground. Track of the underground never joins track of the surface
 * (construction checks both ways); the transformer will be the only way between them.
 */

#ifndef METRO_OVERLAY_H
#define METRO_OVERLAY_H

#include "map_type.h"
#include "tile_type.h"

#include <unordered_map>

/** A full map tile of the underground. */
struct MetroOverlayTile {
	Tile m{};
	TileExtended me{};
};

extern std::unordered_map<uint32_t, MetroOverlayTile> _metro_overlay;

/** Whether the surface tile has an underground tile (rail, depot...) under it. */
inline bool IsMetroOverlayTile(TileIndex tile)
{
	return !_metro_overlay.empty() && _metro_overlay.contains(tile.base());
}

/** Whether the underground tiles are swapped into the map now. */
bool IsMetroOverlayActive();

/** Swaps the underground into the map for its lifetime (nested scopes swap once). */
class MetroUndergroundScope {
	bool active;
public:
	explicit MetroUndergroundScope(bool condition = true);
	~MetroUndergroundScope();
	MetroUndergroundScope(const MetroUndergroundScope &) = delete;
	MetroUndergroundScope &operator=(const MetroUndergroundScope &) = delete;
};

void MetroOverlayReset();
bool MetroOverlayAddTrack(TileIndex tile, uint8_t owner, uint8_t bits);
bool MetroOverlayRemoveTrack(TileIndex tile, uint8_t bits);
void MetroOverlayPrepareBlank(TileIndex tile);
void MetroOverlayDropIfClear(TileIndex tile);
uint8_t MetroOverlayTrackBits(TileIndex tile);
bool MetroOverlayIsDepot(TileIndex tile);
bool MetroOverlayHasTrainOn(TileIndex tile);
bool MetroIsUndergroundVehicleTile(TileIndex tile);
void MetroOverlayDrawTile(struct TileInfo *ti);

#endif /* METRO_OVERLAY_H */
