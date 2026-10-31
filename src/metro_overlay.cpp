/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_overlay.cpp Metro mod: the underground as full map tiles swapped into the map when needed. */

#include "stdafx.h"
#include "metro_overlay.h"
#include "clear_map.h"
#include "company_base.h"
#include "company_gui.h"
#include "rail.h"
#include "rail_map.h"
#include "tile_cmd.h"
#include "train.h"
#include "pathfinder/yapf/yapf_cache.h"

#include "safeguards.h"

std::unordered_map<uint32_t, MetroOverlayTile> _metro_overlay;
static int _metro_overlay_depth = 0; ///< Nesting of #MetroUndergroundScope; swapped in while > 0.

/** Swap one underground tile with the map; the height stays the surface one. */
static void SwapTile(TileIndex t, MetroOverlayTile &o)
{
	uint8_t height = _m[t].height;
	std::swap(_m[t], o.m);
	std::swap(_me[t], o.me);
	_m[t].height = height;
	o.m.height = height;
}

static void SwapAll()
{
	for (auto &[tile, o] : _metro_overlay) SwapTile(TileIndex{tile}, o);
	/* Path finder caches hold the tiles of the other world. */
	YapfNotifyTrackLayoutChange(INVALID_TILE, INVALID_TRACK);
}

bool IsMetroOverlayActive()
{
	return _metro_overlay_depth > 0;
}

MetroUndergroundScope::MetroUndergroundScope(bool condition) : active(condition && !_metro_overlay.empty())
{
	if (!this->active) return;
	if (_metro_overlay_depth++ == 0) SwapAll();
}

MetroUndergroundScope::~MetroUndergroundScope()
{
	if (!this->active) return;
	if (--_metro_overlay_depth == 0) SwapAll();
}

void MetroOverlayReset()
{
	assert(_metro_overlay_depth == 0);
	_metro_overlay.clear();
}

/**
 * Edit one underground tile with the map accessors: it is swapped into the map (a new one starts as clear land)
 * while @p edit runs; clear land afterwards means no underground tile.
 */
template <typename F>
static void EditOverlayTile(TileIndex t, F edit)
{
	assert(_metro_overlay_depth == 0);
	auto [it, inserted] = _metro_overlay.try_emplace(t.base());
	if (inserted) {
		it->second.m = _m[t];
		it->second.me = _me[t];
	}
	SwapTile(t, it->second);
	if (inserted) {
		MakeClear(t, ClearGround::Grass, 3);
		SB(_m[t].type, 2, 2, 0); // No bridge above in the underground.
	}
	edit(t);
	bool clear = IsTileType(t, TileType::Clear);
	SwapTile(t, it->second);
	if (clear) _metro_overlay.erase(it);
}

/** A new underground tile of clear land, for a vanilla command to build on while the underground is swapped in. */
void MetroOverlayPrepareBlank(TileIndex tile)
{
	assert(_metro_overlay_depth == 0);
	auto [it, inserted] = _metro_overlay.try_emplace(tile.base());
	if (!inserted) return;
	it->second.m = _m[tile];
	it->second.me = _me[tile];
	SwapTile(tile, it->second);
	MakeClear(tile, ClearGround::Grass, 3);
	SB(_m[tile].type, 2, 2, 0);
	SwapTile(tile, it->second);
}

/** Forget an underground tile that is (again) clear land. */
void MetroOverlayDropIfClear(TileIndex tile)
{
	auto it = _metro_overlay.find(tile.base());
	if (it != _metro_overlay.end() && GB(it->second.m.type, 4, 4) == to_underlying(TileType::Clear)) _metro_overlay.erase(it);
}

/** Add track pieces to the underground tile (a new one becomes plain rail of the metro). */
bool MetroOverlayAddTrack(TileIndex tile, uint8_t owner, uint8_t bits)
{
	bool ok = true;
	EditOverlayTile(tile, [&](TileIndex t) {
		TrackBits add = static_cast<TrackBits>(bits);
		if (IsTileType(t, TileType::Railway)) {
			if (!IsPlainRail(t)) {
				ok = false;
				return;
			}
			add &= ~GetTrackBits(t);
			SetTrackBits(t, GetTrackBits(t) | add);
		} else {
			MakeRailNormal(t, Owner{owner}, add, RAILTYPE_RAIL);
		}
		if (Company *c = Company::GetIfValid(Owner{owner}); c != nullptr) {
			c->infrastructure.rail[RAILTYPE_RAIL] += CountBits(add);
			DirtyCompanyInfrastructureWindows(c->index);
		}
	});
	YapfNotifyTrackLayoutChange(tile, FindFirstTrack(static_cast<TrackBits>(bits)));
	return ok;
}

/** Remove track pieces from the underground tile; no track left, no tile. */
bool MetroOverlayRemoveTrack(TileIndex tile, uint8_t bits)
{
	if (!IsMetroOverlayTile(tile)) return false;
	bool ok = true;
	EditOverlayTile(tile, [&](TileIndex t) {
		if (!IsTileType(t, TileType::Railway) || !IsPlainRail(t)) {
			ok = false;
			return;
		}
		TrackBits remove = GetTrackBits(t) & static_cast<TrackBits>(bits);
		TrackBits left = GetTrackBits(t) & ~remove;
		Owner owner = GetTileOwner(t);
		if (left == TRACK_BIT_NONE) {
			MakeClear(t, ClearGround::Grass, 3);
		} else {
			SetTrackBits(t, left);
		}
		if (Company *c = Company::GetIfValid(owner); c != nullptr) {
			c->infrastructure.rail[RAILTYPE_RAIL] -= CountBits(remove);
			DirtyCompanyInfrastructureWindows(c->index);
		}
	});
	YapfNotifyTrackLayoutChange(tile, FindFirstTrack(static_cast<TrackBits>(bits)));
	return ok;
}

/** Track pieces of the underground tile (plain rail), 0 for none. */
uint8_t MetroOverlayTrackBits(TileIndex tile)
{
	auto it = _metro_overlay.find(tile.base());
	if (it == _metro_overlay.end()) return 0;
	const Tile &m = it->second.m;
	if (GB(m.type, 4, 4) != to_underlying(TileType::Railway) || GB(m.m5, 6, 2) == 3) return 0;
	return GB(m.m5, 0, 6);
}

/** Whether the underground tile is a depot. */
bool MetroOverlayIsDepot(TileIndex tile)
{
	auto it = _metro_overlay.find(tile.base());
	return it != _metro_overlay.end() && GB(it->second.m.type, 4, 4) == to_underlying(TileType::Railway) && GB(it->second.m.m5, 6, 2) == 3;
}

/** Whether a train of the underground is on the tile. */
bool MetroOverlayHasTrainOn(TileIndex tile)
{
	for (const Train *v : Train::Iterate()) {
		if (v->tile == tile) return true;
	}
	return false;
}

/** Whether a vehicle on the tile is under the ground (hidden in the surface view). */
bool MetroIsUndergroundVehicleTile(TileIndex tile)
{
	return IsMetroOverlayTile(tile) && !IsMetroOverlayActive();
}

/** Draw the real underground tile (rails, signals, depot) in the underground view. */
void MetroOverlayDrawTile(TileInfo *ti)
{
	auto it = _metro_overlay.find(ti->tile.base());
	if (it == _metro_overlay.end() || _metro_overlay_depth != 0) return;
	SwapTile(ti->tile, it->second);
	TileInfo uti = *ti;
	_tile_type_procs[GetTileType(ti->tile)]->draw_tile_proc(&uti, DrawTileProcParams{INT_MIN, false});
	SwapTile(ti->tile, it->second);
}
