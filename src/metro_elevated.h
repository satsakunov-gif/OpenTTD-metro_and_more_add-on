/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_elevated.h Metro mod: elevated monorail.
 *
 * The elevation is a property of the track tile (no separate map array):
 *  - plain rail tile: m8 bits 12..13 the level (0 = ground), bit 14 ramp, bit 15 ramp direction;
 *  - rail station tile: m8 bits 12..13 the level;
 *  - rail bridge head: m7 bits 0..1 the level (bits unused by rail bridges, road bridges keep the road owner there).
 * A level is one TILE_HEIGHT above the ground of the (flat) tile. A ramp tile rises from its level to the next one
 * towards #GetMetroRampUpDir. Spans over houses and roads are bridges between elevated bridge heads.
 */

#ifndef METRO_ELEVATED_H
#define METRO_ELEVATED_H

#include "command_type.h"
#include "direction_type.h"
#include "rail_map.h"
#include "station_map.h"
#include "tunnelbridge_map.h"
#include "track_type.h"

#include <span>
#include <string_view>

struct TileInfo;

static constexpr uint METRO_ELEVATED_MAX_LEVEL = 3; ///< Highest level (2 bits).

/** Level of the elevated track on the tile (0 on the ground or for other tiles). */
inline uint GetMetroElevation(TileIndex t)
{
	switch (GetTileType(t)) {
		case TileType::Railway: return IsPlainRail(t) ? GB(_me[t].m8, 12, 2) : 0;
		case TileType::Station: return IsRailStation(t) ? GB(_me[t].m8, 12, 2) : 0;
		case TileType::TunnelBridge: return (IsBridge(t) && GetTunnelBridgeTransportType(t) == TransportType::Rail) ? GB(_me[t].m7, 0, 2) : 0;
		default: return 0;
	}
}

inline void SetMetroElevation(TileIndex t, uint level)
{
	switch (GetTileType(t)) {
		case TileType::Railway: SB(_me[t].m8, 12, 2, level); break;
		case TileType::Station: SB(_me[t].m8, 12, 2, level); break;
		case TileType::TunnelBridge: SB(_me[t].m7, 0, 2, level); break;
		default: NOT_REACHED();
	}
}

/** Whether the plain rail tile is a ramp of the elevated monorail. */
inline bool IsMetroRamp(TileIndex t)
{
	return IsTileType(t, TileType::Railway) && IsPlainRail(t) && HasBit(_me[t].m8, 14);
}

/** Direction in which the ramp rises (along its track). */
inline DiagDirection GetMetroRampUpDir(TileIndex t)
{
	Axis axis = (GetTrackBits(t) & TRACK_BIT_Y) != TRACK_BIT_NONE ? Axis::Y : Axis::X;
	DiagDirection dir = AxisToDiagDir(axis); // SW for X, SE for Y.
	return HasBit(_me[t].m8, 15) ? ReverseDiagDir(dir) : dir;
}

inline void SetMetroRamp(TileIndex t, bool ramp, DiagDirection up = DiagDirection::SW)
{
	AssignBit(_me[t].m8, 14, ramp);
	AssignBit(_me[t].m8, 15, ramp && (up == DiagDirection::NE || up == DiagDirection::NW));
}

/** Whether the tile carries elevated monorail (any level or a ramp). */
inline bool IsMetroElevatedTile(TileIndex t)
{
	return GetMetroElevation(t) != 0 || IsMetroRamp(t);
}

/* Height of the track. */
int MetroElevatedTrackPixelZ(TileIndex tile, uint x, uint y, int ground_z);

/* Construction rules. */
bool IsMetroElevatedRailType(RailType rt);
CommandCost MetroCheckTrackEdges(TileIndex tile, TrackBits bits);
CommandCost MetroCheckEdgeAgainstZ(TileIndex tile, DiagDirection dir, int z);
CommandCost MetroCheckBridgeAboveHouse(TileIndex tile, int bridge_height);
CommandCost MetroCheckNotElevated(TileIndex tile);
extern uint _metro_elevated_bridge_level;

/* Drawing. */
void MetroDrawElevatedBase(TileInfo *ti, uint level, Axis axis);

/* Saves. */
void MetroElevatedAfterLoad();

/* Window. */
void ShowMetroElevatedToolbar(RailType railtype);
bool IsMetroElevatedAvailable(RailType railtype);

bool ConMetroElevated(std::span<std::string_view> argv);

#endif /* METRO_ELEVATED_H */
