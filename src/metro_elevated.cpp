/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_elevated.cpp Metro mod: elevated monorail.
 *
 * Monorail track and stations can be raised to level 2 or 3 above the ground (flat tiles only). Ramps (one level per
 * tile) join them to the ground; spans over houses, roads, rails and water are bridges between elevated bridge heads,
 * at most #METRO_ELEVATED_MAX_SPAN tiles long. Track at different heights never joins: every construction checks the
 * height where its track meets the neighbour's track (#MetroCheckTrackEdges).
 */

#include "stdafx.h"
#include "metro_elevated.h"
#include "metro_elevated_cmd.h"
#include "bridge.h"
#include "bridge_map.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "console_func.h"
#include "console_type.h"
#include "date_func.h"
#include "economy_func.h"
#include "gfx_func.h"
#include "gui.h"
#include "house.h"
#include "landscape.h"
#include "rail.h"
#include "slope_func.h"
#include "sprite.h"
#include "strings_func.h"
#include "tilehighlight_func.h"
#include "town.h"
#include "town_map.h"
#include "transparency.h"
#include "track_func.h"
#include "tunnelbridge_cmd.h"
#include "vehicle_func.h"
#include "viewport_func.h"
#include "window_func.h"
#include "window_gui.h"
#include "tile_cmd.h"
#include "engine_base.h"
#include "engine_func.h"
#include "rail_cmd.h"
#include "station_cmd.h"
#include "newgrf_station.h"
#include "tilearea_type.h"
#include "train.h"
#include "vehicle_cmd.h"
#include "tile_track_func.h"
#include "core/backup_type.hpp"
#include "core/string_consumer.hpp"
#include "table/metro_elevated_tables.h"
#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

uint _metro_elevated_bridge_level = 0;

/* ---------------------------------------------------------------------------------------------- */
/* Height                                                                                         */
/* ---------------------------------------------------------------------------------------------- */

/** Pixel height of the track on an elevated plain rail tile at (x, y); @p ground_z is the height of the flat tile. */
int MetroElevatedTrackPixelZ(TileIndex tile, uint x, uint y, int ground_z)
{
	int z = ground_z + GetMetroElevation(tile) * TILE_HEIGHT;
	if (IsMetroRamp(tile)) z += GetPartialPixelZ(x & 0xF, y & 0xF, InclinedSlope(GetMetroRampUpDir(tile)));
	return z;
}

bool IsMetroElevatedRailType(RailType rt)
{
	return rt < RAILTYPE_END && GetRailTypeInfo(rt)->label == RAILTYPE_LABEL_MONO;
}

bool IsMetroElevatedAvailable(RailType railtype)
{
	return Company::IsValidID(_local_company) && IsMetroElevatedRailType(railtype) && HasRailTypeAvail(_local_company, railtype);
}

/* ---------------------------------------------------------------------------------------------- */
/* Joints between tiles                                                                           */
/* ---------------------------------------------------------------------------------------------- */

/** Rail track bits of @p tile that reach its edge @p dir (none on the bridge or tunnel side of a head). */
static TrackBits GetRailTrackBitsAtEdge(TileIndex tile, DiagDirection dir)
{
	if (!IsValidTile(tile)) return TRACK_BIT_NONE;
	if (IsTileType(tile, TileType::TunnelBridge)) {
		if (GetTunnelBridgeTransportType(tile) != TransportType::Rail || dir == GetTunnelBridgeDirection(tile)) return TRACK_BIT_NONE;
	}
	TrackBits bits = TrackdirBitsToTrackBits(GetTileTrackStatus(tile, TransportType::Rail, 0).trackdirs);
	return bits & DiagdirReachesTracks(ReverseDiagDir(dir));
}

/** Height of the track where it crosses the edge @p dir of @p tile. */
static int GetEdgePixelZ(TileIndex tile, DiagDirection dir)
{
	TileIndexDiffC d = TileIndexDiffCByDiagDir(dir);
	auto offset = [](int step) { return step > 0 ? static_cast<int>(TILE_SIZE) - 1 : (step < 0 ? 0 : static_cast<int>(TILE_SIZE) / 2); };
	return GetSlopePixelZ(TileX(tile) * TILE_SIZE + offset(d.x), TileY(tile) * TILE_SIZE + offset(d.y), true);
}

/** The heights of two joined tracks differ by less than a level. */
static bool SameHeight(int z1, int z2)
{
	return std::abs(z1 - z2) <= 2;
}

/**
 * Check that the track @p bits of @p tile meet the neighbouring track at the same height,
 * when the tile or its neighbour is elevated.
 */
CommandCost MetroCheckTrackEdges(TileIndex tile, TrackBits bits)
{
	for (DiagDirection dir : EnumRange(DiagDirection::End)) {
		if ((bits & DiagdirReachesTracks(ReverseDiagDir(dir))) == TRACK_BIT_NONE) continue;
		TileIndex next = TileAddByDiagDir(tile, dir);
		if (!IsValidTile(next)) continue;
		DiagDirection back = ReverseDiagDir(dir);
		if (GetRailTrackBitsAtEdge(next, back) == TRACK_BIT_NONE) continue;
		if (!IsMetroElevatedTile(tile) && !IsMetroElevatedTile(next)) continue;
		if (!SameHeight(GetEdgePixelZ(tile, dir), GetEdgePixelZ(next, back))) return CommandCost(STR_METRO_ELEVATED_ERR_HEIGHT);
	}
	return CommandCost();
}

/** Check that track ending at the edge @p dir of @p tile at height @p z meets the neighbour's track there at the same height. */
CommandCost MetroCheckEdgeAgainstZ(TileIndex tile, DiagDirection dir, int z)
{
	TileIndex next = TileAddByDiagDir(tile, dir);
	if (!IsValidTile(next)) return CommandCost();
	DiagDirection back = ReverseDiagDir(dir);
	if (GetRailTrackBitsAtEdge(next, back) == TRACK_BIT_NONE) return CommandCost();
	if (!SameHeight(GetEdgePixelZ(next, back), z)) return CommandCost(STR_METRO_ELEVATED_ERR_HEIGHT);
	return CommandCost();
}

/** A tile with elevated track cannot be changed this way. */
CommandCost MetroCheckNotElevated(TileIndex tile)
{
	if (IsMetroElevatedTile(tile)) return CommandCost(STR_METRO_ELEVATED_ERR_ELEVATED);
	return CommandCost();
}

/** A span at @p bridge_height (levels) may pass over the house on @p tile: not a high-rise, enough room. */
CommandCost MetroCheckBridgeAboveHouse(TileIndex tile, int bridge_height)
{
	HouseID house = GetHouseType(tile);
	GetHouseNorthPart(house);
	if (HouseSpec::Get(house)->population >= METRO_ELEVATED_TALL_HOUSE_POPULATION) return CommandCost(STR_METRO_ELEVATED_ERR_TALL_HOUSE);
	if (GetTileMaxZ(tile) + static_cast<int>(METRO_ELEVATED_HOUSE_CLEARANCE) > bridge_height) return CommandCost(STR_METRO_ELEVATED_ERR_LOW_OVER_HOUSE);
	return CommandCost();
}

/* ---------------------------------------------------------------------------------------------- */
/* Drawing                                                                                        */
/* ---------------------------------------------------------------------------------------------- */

/**
 * Draw the ground and the pillars under an elevated tile and lift the following ground sprites (the track) to the
 * elevated height, like a foundation does. Raises @p ti->z to the track.
 */
void MetroDrawElevatedBase(TileInfo *ti, uint level, Axis axis)
{
	static constexpr int BRIDGE_Z_START = 3;
	static constexpr int PILLAR_Z_OFFSET = TILE_HEIGHT - BRIDGE_Z_START;

	DrawGroundSprite(SPR_FLAT_GRASS_TILE, PAL_NONE);

	int deck = ti->z + static_cast<int>(level * TILE_HEIGHT);
	if (!IsInvisibilitySet(TransparencyOption::Bridges)) {
		/* One pillar in the middle of the tile, under the beam. */
		SpriteID pillar = axis == Axis::X ? METRO_ELEVATED_PILLAR_X : METRO_ELEVATED_PILLAR_Y;
		uint8_t w = axis == Axis::X ? TILE_SIZE : 2;
		uint8_t h = axis == Axis::X ? 2 : TILE_SIZE;
		bool transparent = IsTransparencySet(TransparencyOption::Bridges);
		int x = ti->x + (axis == Axis::X ? 0 : METRO_ELEVATED_PILLAR_OFFSET);
		int y = ti->y + (axis == Axis::X ? METRO_ELEVATED_PILLAR_OFFSET : 0);
		for (int z = deck - BRIDGE_Z_START - static_cast<int>(TILE_HEIGHT); z >= ti->z - BRIDGE_Z_START; z -= TILE_HEIGHT) {
			AddSortableSpriteToDraw(pillar, PAL_NONE, x, y, z, {{0, 0, -PILLAR_Z_OFFSET}, {w, h, BB_HEIGHT_UNDER_BRIDGE}, {0, 0, PILLAR_Z_OFFSET}}, transparent);
		}
	}

	/* The deck: the parent sprite of the track sprites drawn next. */
	AddSortableSpriteToDraw(SPR_EMPTY_BOUNDING_BOX, PAL_NONE, ti->x, ti->y, deck - static_cast<int>(TILE_HEIGHT), {{}, {TILE_SIZE, TILE_SIZE, TILE_HEIGHT - 1}, {}});
	OffsetGroundSprite(0, -static_cast<int>(TILE_HEIGHT));
	ti->z = deck;
}

/** Draw the beam of the elevated monorail: single rail pieces without ground (vanilla sprites for now). */
void MetroDrawElevatedPieces(const TileInfo *ti, RailType rt, TrackBits bits)
{
	const RailTypeInfo *rti = GetRailTypeInfo(rt);
	if (ti->tileh != SLOPE_FLAT) {
		/* A ramp: the sloped piece rising towards the slope. */
		DiagDirection up = DiagDirection::NE;
		switch (ti->tileh) {
			case SLOPE_NE: up = DiagDirection::NE; break;
			case SLOPE_SE: up = DiagDirection::SE; break;
			case SLOPE_SW: up = DiagDirection::SW; break;
			case SLOPE_NW: up = DiagDirection::NW; break;
			default: break;
		}
		DrawGroundSprite(rti->base_sprites.single_sloped + to_underlying(up), PAL_NONE);
		return;
	}
	const SpriteID pieces[] = {
		rti->base_sprites.single_x, rti->base_sprites.single_y, rti->base_sprites.single_n,
		rti->base_sprites.single_s, rti->base_sprites.single_w, rti->base_sprites.single_e,
	};
	for (Track t : SetTrackBitIterator(bits)) DrawGroundSprite(pieces[t], PAL_NONE);
}

/** Draw an elevated plain rail tile or ramp: ground, pillar and beam. */
void MetroDrawElevatedTrack(TileInfo *ti, TrackBits rails)
{
	uint level = GetMetroElevation(ti->tile);
	Axis axis = (rails & TRACK_BIT_Y) != TRACK_BIT_NONE && (rails & TRACK_BIT_X) == TRACK_BIT_NONE ? Axis::Y : Axis::X;
	if (level != 0) {
		MetroDrawElevatedBase(ti, level, axis);
	} else {
		DrawGroundSprite(SPR_FLAT_GRASS_TILE, PAL_NONE); // Foot of a ramp.
	}
	if (IsMetroRamp(ti->tile)) ti->tileh = InclinedSlope(GetMetroRampUpDir(ti->tile));
	MetroDrawElevatedPieces(ti, GetRailType(ti->tile), rails);
}

/* ---------------------------------------------------------------------------------------------- */
/* Commands                                                                                       */
/* ---------------------------------------------------------------------------------------------- */

/** Rail track bits of a rail, station or bridge head tile. */
static TrackBits GetTileRailBits(TileIndex tile)
{
	return TrackdirBitsToTrackBits(GetTileTrackStatus(tile, TransportType::Rail, 0).trackdirs);
}

/** Common checks of a monorail tile of the current company that gets a new height. */
static CommandCost CheckElevatableTile(TileIndex tile)
{
	CommandCost ret = CheckTileOwnership(tile);
	if (ret.Failed()) return ret;
	if (!IsMetroElevatedRailType(GetRailType(tile))) return CommandCost(STR_METRO_ELEVATED_ERR_NOT_MONORAIL);
	if (GetTileSlope(tile) != SLOPE_FLAT) return CommandCost(STR_METRO_ELEVATED_ERR_FLAT);
	if (IsBridgeAbove(tile)) return CommandCost(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);
	return EnsureNoVehicleOnGround(tile);
}

/** Saved height of a tile, to undo a trial change. */
struct ElevationBackup {
	TileIndex tile;
	uint level;
	bool ramp;
	DiagDirection up;
};

static void RestoreElevations(const std::vector<ElevationBackup> &backup)
{
	for (const ElevationBackup &b : backup) {
		SetMetroElevation(b.tile, b.level);
		if (IsTileType(b.tile, TileType::Railway)) SetMetroRamp(b.tile, b.ramp, b.up);
	}
}

static ElevationBackup BackupElevation(TileIndex tile)
{
	bool ramp = IsMetroRamp(tile);
	return {tile, GetMetroElevation(tile), ramp, ramp ? GetMetroRampUpDir(tile) : DiagDirection::SW};
}

/** After a trial change of the tiles: check their joints, then keep (execute) or undo the change. */
static CommandCost FinishElevationChange(DoCommandFlags flags, const std::vector<ElevationBackup> &backup, CommandCost cost)
{
	for (const ElevationBackup &b : backup) {
		CommandCost ret = MetroCheckTrackEdges(b.tile, GetTileRailBits(b.tile));
		if (ret.Failed()) {
			RestoreElevations(backup);
			return ret;
		}
	}
	if (!flags.Test(DoCommandFlag::Execute)) {
		RestoreElevations(backup);
		return cost;
	}
	for (const ElevationBackup &b : backup) MarkTileDirtyByTile(b.tile, VMDF_NONE, METRO_ELEVATED_MAX_LEVEL + 1);
	return cost;
}

/**
 * Raise monorail track and stations of a rectangle to @p level, or lower them to the ground (level 0, also ramps).
 * @param flags type of operation
 * @param tile one corner of the rectangle
 * @param start other corner
 * @param level 0, or #METRO_ELEVATED_MIN_LEVEL .. #METRO_ELEVATED_MAX_LEVEL
 */
CommandCost CmdMetroElevateRail(DoCommandFlags flags, TileIndex tile, TileIndex start, uint8_t level)
{
	if (start >= Map::Size() || level > METRO_ELEVATED_MAX_LEVEL || (level != 0 && level < METRO_ELEVATED_MIN_LEVEL)) return CMD_ERROR;

	CommandCost cost(ExpensesType::Construction);
	CommandCost last_error(STR_METRO_ELEVATED_ERR_NO_TRACK);
	std::vector<ElevationBackup> backup;
	for (TileIndex t : TileArea(start, tile)) {
		bool rail = IsPlainRailTile(t);
		if (!rail && !IsRailStationTile(t)) continue;
		if (GetMetroElevation(t) == level && !IsMetroRamp(t)) continue;
		if (level != 0 && IsMetroRamp(t)) {
			last_error = CommandCost(STR_METRO_ELEVATED_ERR_RAMP_HERE);
			continue;
		}
		CommandCost ret = CheckElevatableTile(t);
		if (ret.Failed()) {
			last_error = ret;
			continue;
		}
		uint old_level = GetMetroElevation(t);
		if (level > old_level) {
			Money price = rail ? RailBuildCost(GetRailType(t)) * CountBits(GetTrackBits(t)) : _price[Price::BuildStationRail];
			cost.AddCost(price * METRO_ELEVATED_COST_PERCENT_PER_LEVEL / 100 * (level - old_level));
		}
		backup.push_back(BackupElevation(t));
		SetMetroElevation(t, level);
		if (rail) SetMetroRamp(t, false);
	}
	if (backup.empty()) return last_error;
	return FinishElevationChange(flags, backup, cost);
}

/** Whether rail track on the ground joins the edge @p dir of @p tile from outside (a ramp is needed there). */
static bool HasGroundTrackBeyond(TileIndex tile, DiagDirection dir)
{
	TileIndex next = TileAddByDiagDir(tile, dir);
	if (!IsValidTile(next) || IsMetroElevatedTile(next)) return false;
	return GetRailTrackBitsAtEdge(next, ReverseDiagDir(dir)) != TRACK_BIT_NONE;
}

/**
 * Raise a straight stretch of monorail track (and stations along it) to @p level; where it meets track on the
 * ground, its end tiles become ramps, one level per tile.
 * @param flags type of operation
 * @param tile one end
 * @param start other end
 * @param level #METRO_ELEVATED_MIN_LEVEL .. #METRO_ELEVATED_MAX_LEVEL
 */
CommandCost CmdMetroElevateLine(DoCommandFlags flags, TileIndex tile, TileIndex start, uint8_t level)
{
	if (start >= Map::Size() || level < METRO_ELEVATED_MIN_LEVEL || level > METRO_ELEVATED_MAX_LEVEL) return CMD_ERROR;
	if (start == tile || (TileX(start) != TileX(tile) && TileY(start) != TileY(tile))) return CommandCost(STR_METRO_ELEVATED_ERR_LINE);

	DiagDirection dir = DiagdirBetweenTiles(start, tile);
	Axis axis = DiagDirToAxis(dir);
	uint length = DistanceManhattan(start, tile) + 1;
	bool ramp_start = HasGroundTrackBeyond(start, ReverseDiagDir(dir));
	bool ramp_end = HasGroundTrackBeyond(tile, dir);
	if (length < (ramp_start ? level : 0u) + (ramp_end ? level : 0u)) return CommandCost(STR_METRO_ELEVATED_ERR_LINE_SHORT);

	CommandCost cost(ExpensesType::Construction);
	std::vector<ElevationBackup> backup;
	TileIndex t = start;
	for (uint i = 0; i < length; i++, t = TileAddByDiagDir(t, dir)) {
		bool ramp_up = ramp_start && i < level;
		bool ramp_down = ramp_end && length - 1 - i < level;
		bool rail = IsPlainRailTile(t);
		bool ok = rail ? GetTrackBits(t) == AxisToTrackBits(axis) : (IsRailStationTile(t) && GetRailStationAxis(t) == axis);
		if (ok && (ramp_up || ramp_down) && (!rail || HasSignals(t))) ok = false;
		if (!ok) {
			RestoreElevations(backup);
			return CommandCost(STR_METRO_ELEVATED_ERR_LINE_TRACK);
		}
		CommandCost ret = CheckElevatableTile(t);
		if (ret.Failed()) {
			RestoreElevations(backup);
			return ret;
		}
		bool ramp = ramp_up || ramp_down;
		uint new_level = ramp_up ? i : (ramp_down ? length - 1 - i : level);
		uint new_top = new_level + (ramp ? 1 : 0);
		uint old_top = GetMetroElevation(t) + (IsMetroRamp(t) ? 1 : 0);
		if (new_top > old_top) {
			Money price = rail ? RailBuildCost(GetRailType(t)) : _price[Price::BuildStationRail];
			cost.AddCost(price * METRO_ELEVATED_COST_PERCENT_PER_LEVEL / 100 * (new_top - old_top));
		}
		backup.push_back(BackupElevation(t));
		SetMetroElevation(t, new_level);
		if (rail) SetMetroRamp(t, ramp, ramp_up ? dir : ReverseDiagDir(dir));
	}
	return FinishElevationChange(flags, backup, cost);
}

/** The fastest bridge type available for a span of @p length tiles, or MAX_BRIDGES. */
static BridgeType GetElevatedSpanBridgeType(uint length)
{
	BridgeType best = MAX_BRIDGES;
	for (BridgeType b = 0; b < MAX_BRIDGES; b++) {
		if (CheckBridgeAvailability(b, length).Failed()) continue;
		if (best == MAX_BRIDGES || GetBridgeSpec(b)->speed > GetBridgeSpec(best)->speed) best = b;
	}
	return best;
}

/**
 * Build a span of the elevated monorail: a bridge at @p level between two elevated bridge heads (pillars).
 * @param flags type of operation
 * @param tile one end
 * @param start other end
 * @param level height of the span
 * @param railtype monorail type
 */
CommandCost CmdMetroBuildElevatedSpan(DoCommandFlags flags, TileIndex tile, TileIndex start, uint8_t level, RailType railtype)
{
	if (start >= Map::Size() || level < METRO_ELEVATED_MIN_LEVEL || level > METRO_ELEVATED_MAX_LEVEL) return CMD_ERROR;
	if (!IsMetroElevatedRailType(railtype) || !ValParamRailType(railtype)) return CommandCost(STR_METRO_ELEVATED_ERR_NOT_MONORAIL);
	if (start == tile || (TileX(start) != TileX(tile) && TileY(start) != TileY(tile))) return CommandCost(STR_ERROR_START_AND_END_MUST_BE_IN);
	uint length = DistanceManhattan(start, tile) - 1;
	if (length > METRO_ELEVATED_MAX_SPAN) return CommandCost(STR_METRO_ELEVATED_ERR_SPAN_LENGTH);
	for (TileIndex head : {start, tile}) {
		if (GetTileSlope(head) != SLOPE_FLAT) return CommandCost(STR_METRO_ELEVATED_ERR_FLAT);
	}
	BridgeType type = GetElevatedSpanBridgeType(length);
	if (type == MAX_BRIDGES) return CommandCost(STR_ERROR_CAN_T_BUILD_BRIDGE_HERE);

	/* The heads continue track at the deck height on their outer sides. */
	DiagDirection outward = DiagdirBetweenTiles(tile, start);
	int deck_start = (GetTileZ(start) + level) * TILE_HEIGHT;
	int deck_end = (GetTileZ(tile) + level) * TILE_HEIGHT;
	CommandCost ret = MetroCheckEdgeAgainstZ(start, outward, deck_start);
	if (ret.Failed()) return ret;
	ret = MetroCheckEdgeAgainstZ(tile, ReverseDiagDir(outward), deck_end);
	if (ret.Failed()) return ret;

	_metro_elevated_bridge_level = level;
	CommandCost cost = Command<Commands::BuildBridge>::Do(flags, tile, start, TransportType::Rail, type, railtype, BuildBridgeFlags::None);
	_metro_elevated_bridge_level = 0;
	if (cost.Failed()) return cost;
	return CommandCost(cost.GetExpensesType(), cost.GetCost() * METRO_ELEVATED_SPAN_COST_PERCENT / 100);
}

/* ---------------------------------------------------------------------------------------------- */
/* Saves                                                                                          */
/* ---------------------------------------------------------------------------------------------- */

/** Saves from before the elevated monorail: clear the bits it uses (they had no meaning). */
void MetroElevatedAfterLoad()
{
	for (TileIndex t(0); t < Map::Size(); t++) {
		switch (GetTileType(t)) {
			case TileType::Railway:
				if (IsPlainRail(t)) SB(_me[t].m8, 12, 4, 0);
				break;
			case TileType::Station:
				if (IsRailStation(t)) SB(_me[t].m8, 12, 2, 0);
				break;
			case TileType::TunnelBridge:
				if (IsBridge(t) && GetTunnelBridgeTransportType(t) == TransportType::Rail) SB(_me[t].m7, 0, 2, 0);
				break;
			default:
				break;
		}
	}
}

/* ---------------------------------------------------------------------------------------------- */
/* Construction window                                                                            */
/* ---------------------------------------------------------------------------------------------- */

/** Widgets of the #MetroElevatedToolbarWindow. */
enum MetroElevatedToolbarWidgets : WidgetID {
	WID_MET_LIST, ///< Level and tools.
	WID_MET_INFO, ///< Hint.
};

static constexpr std::initializer_list<NWidgetPart> _nested_metro_elevated_toolbar_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::DarkGreen),
		NWidget(WWT_CAPTION, Colours::DarkGreen), SetStringTip(STR_METRO_ELEVATED_TOOLBAR_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::DarkGreen),
		NWidget(WWT_STICKYBOX, Colours::DarkGreen),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::DarkGreen, WID_MET_LIST), SetMinimalSize(360, 0), SetFill(1, 0), SetResize(1, 0), SetToolTip(STR_METRO_ELEVATED_TOOLBAR_LIST_TOOLTIP), EndContainer(),
	NWidget(WWT_PANEL, Colours::DarkGreen, WID_MET_INFO), SetMinimalSize(360, 0), SetFill(1, 1), SetResize(1, 0), EndContainer(),
};

/** Rows of the window. */
enum MetroElevatedRow : int {
	MER_LEVEL, ///< Level of the next construction (click to switch).
	MER_LINE,  ///< Raise a straight stretch, ramps at its ends.
	MER_RAISE, ///< Raise track and stations.
	MER_LOWER, ///< Lower them to the ground.
	MER_SPAN,  ///< Span.
	MER_COUNT,
};

/** Construction of the elevated monorail: choose the level, a tool, then the map. */
struct MetroElevatedToolbarWindow : Window {
	int selected = -1;
	uint8_t level = METRO_ELEVATED_MIN_LEVEL;
	RailType railtype;

	MetroElevatedToolbarWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc), railtype(static_cast<RailType>(window_number))
	{
		this->InitNested(window_number);
	}

	std::string GetRowText(int row) const
	{
		switch (row) {
			case MER_LEVEL: return GetString(STR_METRO_ELEVATED_ROW_LEVEL, this->level);
			case MER_LINE: return GetString(STR_METRO_ELEVATED_ROW_LINE, this->level, this->level);
			case MER_RAISE: return GetString(STR_METRO_ELEVATED_ROW_RAISE, this->level, RailBuildCost(this->railtype) * METRO_ELEVATED_COST_PERCENT_PER_LEVEL / 100);
			case MER_LOWER: return GetString(STR_METRO_ELEVATED_ROW_LOWER);
			case MER_SPAN: return GetString(STR_METRO_ELEVATED_ROW_SPAN, METRO_ELEVATED_MAX_SPAN);
			default: NOT_REACHED();
		}
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		switch (widget) {
			case WID_MET_LIST:
				size.height = MER_COUNT * GetCharacterHeight(FontSize::Normal) + padding.height;
				break;
			case WID_MET_INFO:
				size.height = std::max<uint>(size.height, GetStringHeight(STR_METRO_ELEVATED_TOOLBAR_HINT, size.width - padding.width) + padding.height);
				break;
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		switch (widget) {
			case WID_MET_LIST:
				for (int row = 0; row < MER_COUNT; row++) {
					if (row == this->selected) {
						GfxFillRect(r.left + 1, ir.top, r.right - 1, ir.top + GetCharacterHeight(FontSize::Normal) - 1, PixelColour{GetColourGradient(Colours::DarkGreen, Shade::Lighter)});
					}
					DrawString(ir, this->GetRowText(row), TextColour::Black);
					ir.top += GetCharacterHeight(FontSize::Normal);
				}
				break;
			case WID_MET_INFO:
				DrawStringMultiLine(ir, STR_METRO_ELEVATED_TOOLBAR_HINT);
				break;
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget != WID_MET_LIST) return;
		int row = this->GetRowFromWidget(pt.y, WID_MET_LIST, WidgetDimensions::scaled.framerect.top, GetCharacterHeight(FontSize::Normal));
		if (row < 0 || row >= MER_COUNT) return;
		if (row == MER_LEVEL) {
			this->level = this->level >= METRO_ELEVATED_MAX_LEVEL ? METRO_ELEVATED_MIN_LEVEL : this->level + 1;
			this->SetDirty();
			return;
		}
		if (this->selected == row) {
			ResetObjectToPlace();
			return;
		}
		CursorID cursor = row == MER_LOWER ? SPR_CURSOR_DEMOLISH_FIRST : (row == MER_SPAN ? SPR_CURSOR_BRIDGE : SPR_CURSOR_NS_TRACK);
		SetObjectToPlaceWnd(cursor, PAL_NONE, HT_RECT, this);
		this->selected = row;
		this->SetDirty();
	}

	void OnPlaceObject([[maybe_unused]] Point pt, TileIndex tile) override
	{
		switch (this->selected) {
			case MER_LINE: VpStartPlaceSizing(tile, VPM_X_OR_Y, DDSP_METRO_ELEVATED_LINE); break;
			case MER_RAISE: VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_METRO_ELEVATED_RAISE); break;
			case MER_LOWER: VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_METRO_ELEVATED_LOWER); break;
			case MER_SPAN: VpStartPlaceSizing(tile, VPM_X_OR_Y, DDSP_METRO_ELEVATED_SPAN); break;
			default: break;
		}
	}

	void OnPlaceDrag(ViewportPlaceMethod select_method, [[maybe_unused]] ViewportDragDropSelectionProcess select_proc, Point pt) override
	{
		VpSelectTilesWithMethod(pt.x, pt.y, select_method);
	}

	void OnPlaceMouseUp([[maybe_unused]] ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, Point pt, TileIndex start_tile, TileIndex end_tile) override
	{
		if (pt.x == -1) return;
		switch (select_proc) {
			case DDSP_METRO_ELEVATED_LINE:
				Command<Commands::MetroElevateLine>::Post(STR_METRO_ELEVATED_ERR_CANT_RAISE, end_tile, start_tile, this->level);
				break;
			case DDSP_METRO_ELEVATED_RAISE:
				Command<Commands::MetroElevateRail>::Post(STR_METRO_ELEVATED_ERR_CANT_RAISE, end_tile, start_tile, this->level);
				break;
			case DDSP_METRO_ELEVATED_LOWER:
				Command<Commands::MetroElevateRail>::Post(STR_METRO_ELEVATED_ERR_CANT_LOWER, end_tile, start_tile, 0);
				break;
			case DDSP_METRO_ELEVATED_SPAN:
				Command<Commands::MetroBuildElevatedSpan>::Post(STR_METRO_ELEVATED_ERR_CANT_SPAN, end_tile, start_tile, this->level, this->railtype);
				break;
			default:
				break;
		}
	}

	void OnPlaceObjectAbort() override
	{
		this->selected = -1;
		this->SetDirty();
	}
};

static WindowDesc _metro_elevated_toolbar_desc(__FILE__, __LINE__,
	WindowPosition::Automatic, "metro_elevated_toolbar", 0, 0,
	WindowClass::MetroElevatedToolbar, WindowClass::None,
	WindowDefaultFlag::Construction,
	_nested_metro_elevated_toolbar_widgets
);

void ShowMetroElevatedToolbar(RailType railtype)
{
	if (!IsMetroElevatedAvailable(railtype)) return;
	AllocateWindowDescFront<MetroElevatedToolbarWindow>(_metro_elevated_toolbar_desc, railtype);
}

/* ---------------------------------------------------------------------------------------------- */
/* Console                                                                                        */
/* ---------------------------------------------------------------------------------------------- */

static std::optional<TileIndex> ParseConsoleTile(std::string_view x, std::string_view y)
{
	auto tx = ParseInteger<uint>(x);
	auto ty = ParseInteger<uint>(y);
	if (!tx.has_value() || !ty.has_value() || *tx >= Map::SizeX() || *ty >= Map::SizeY()) return std::nullopt;
	return TileXY(*tx, *ty);
}

/** Company of the console commands: the local one, else the first (tests without a window). */
static CompanyID GetConsoleCompany()
{
	if (Company::IsValidID(_local_company)) return _local_company;
	for (const Company *c : Company::Iterate()) return c->index;
	return CompanyID::Invalid();
}

/** Run a command for the local company from the console; prints the error. */
template <Commands Tcmd, typename... Targs>
static void ConsoleCommand(Targs... args)
{
	Backup<CompanyID> cur_company(_current_company, GetConsoleCompany(), FILE_LINE);
	CommandCost ret = Command<Tcmd>::Do(DoCommandFlag::Execute, args...);
	cur_company.Restore();
	if (ret.Failed()) {
		IConsolePrint(CC_ERROR, "Failed: {}", GetString(ret.GetErrorMessage()));
	} else {
		IConsolePrint(CC_INFO, "Done, cost {}.", ret.GetCost());
	}
}

bool ConMetroElevated(std::span<std::string_view> argv)
{
	if (argv.size() < 2) {
		IConsolePrint(CC_HELP, "Metro mod elevated monorail. Usage: metro_elevated info <x> <y> | raise <x1> <y1> <x2> <y2> <level 0/2/3> | line <x1> <y1> <x2> <y2> <level> | span <x1> <y1> <x2> <y2> <level> | window");
		return true;
	}
	std::string_view cmd = argv[1];
	if (cmd == "testline" && argv.size() > 3) {
		/*
		 * For tests: a monorail line of 34 tiles going +x from a free row at or south of (x, y):
		 * depot, ground x..x+2, ramp up x+3..x+4, raised x+5..x+8, span heads x+9 and x+16 (6 tiles between),
		 * raised x+17..x+20, ramp down x+21..x+22, ground to x+33, and a running monorail engine.
		 */
		auto start = ParseConsoleTile(argv[2], argv[3]);
		if (!start.has_value() || TileX(*start) < 2 || TileX(*start) + 36 >= Map::SizeX()) return false;
		RailType rt = GetRailTypeByLabel(RAILTYPE_LABEL_MONO);
		const Engine *engine = nullptr;
		for (const Engine *e : Engine::IterateType(VehicleType::Train)) {
			if (!IsEngineBuildable(e->index, VehicleType::Train, GetConsoleCompany()) || e->VehInfo<RailVehicleInfo>().power == 0) continue;
			if (e->VehInfo<RailVehicleInfo>().railtypes.Test(rt)) {
				engine = e;
				break;
			}
		}
		if (rt == INVALID_RAILTYPE || engine == nullptr || !HasRailTypeAvail(GetConsoleCompany(), rt)) {
			IConsolePrint(CC_ERROR, "No monorail or monorail engine available this year (rail type {}, engine {}, company {}).", rt, engine != nullptr, GetConsoleCompany());
			return true;
		}
		uint x = TileX(*start);
		DoCommandFlags exec{DoCommandFlag::Execute, DoCommandFlag::Bankrupt};
		DoCommandFlags test{DoCommandFlag::Bankrupt};
		Backup<CompanyID> cur_company(_current_company, GetConsoleCompany(), FILE_LINE);
		/* A free row: flat and level everywhere the line is raised, and room for the track. */
		uint y = TileY(*start);
		bool found = false;
		for (uint row = 0; row < Map::SizeY() - 2 && !found; row++) {
			y = 1 + (TileY(*start) + row) % (Map::SizeY() - 2);
			for (x = 2; x + 36 < Map::SizeX() && !found; x += 4) {
				bool flat = true;
				for (uint i = 3; i <= 33 && flat; i++) {
					flat = GetTileSlope(TileXY(x + i, y)) == SLOPE_FLAT && GetTileZ(TileXY(x + i, y)) == GetTileZ(TileXY(x + 3, y));
				}
				if (!flat) continue;
				if (Command<Commands::BuildRailLong>::Do(test, TileXY(x + 33, y), TileXY(x, y), rt, TRACK_X, BuildRailTrackFlags{}, true).Failed()) continue;
				if (Command<Commands::BuildRailDepot>::Do(test, TileXY(x - 1, y), rt, DiagDirection::SW).Failed()) continue;
				found = true;
			}
		}
		if (found) x -= 4;
		auto step = [&](const char *what, CommandCost ret) {
			if (ret.Failed()) IConsolePrint(CC_ERROR, "{}: {}", what, GetString(ret.GetErrorMessage()));
			return ret.Succeeded();
		};
		bool ok = found;
		if (!found) IConsolePrint(CC_ERROR, "No free flat row.");
		auto t = [&](uint dx) { return TileXY(x + dx, y); };
		if (ok) ok = step("track west", Command<Commands::BuildRailLong>::Do(exec, t(8), t(0), rt, TRACK_X, BuildRailTrackFlags{}, true));
		if (ok) ok = step("track east", Command<Commands::BuildRailLong>::Do(exec, t(33), t(17), rt, TRACK_X, BuildRailTrackFlags{}, true));
		if (ok) ok = step("depot", Command<Commands::BuildRailDepot>::Do(exec, TileXY(x - 1, y), rt, DiagDirection::SW));
		if (ok) ok = step("raise west", Command<Commands::MetroElevateLine>::Do(exec, t(8), t(3), static_cast<uint8_t>(METRO_ELEVATED_MIN_LEVEL)));
		if (ok) ok = step("raise east", Command<Commands::MetroElevateLine>::Do(exec, t(22), t(17), static_cast<uint8_t>(METRO_ELEVATED_MIN_LEVEL)));
		if (ok) ok = step("span", Command<Commands::MetroBuildElevatedSpan>::Do(exec, t(16), t(9), static_cast<uint8_t>(METRO_ELEVATED_MIN_LEVEL), rt));
		/* An elevated station at the dead end: platforms x+29..x+31, raised with a ramp from x+24. */
		if (ok) ok = step("station", Command<Commands::BuildRailStation>::Do(exec, t(29), rt, Axis::X, 1, 3, STAT_CLASS_DFLT, 0, StationID::Invalid(), false));
		if (ok) ok = step("raise station", Command<Commands::MetroElevateLine>::Do(exec, t(33), t(24), static_cast<uint8_t>(METRO_ELEVATED_MIN_LEVEL)));
		std::optional<VehicleID> vehicle;
		if (ok) {
			CommandCost ret = Command<Commands::BuildVehicle>::Do(DoCommandFlag::Execute, TileXY(x - 1, y), engine->index, true, INVALID_CARGO, ClientID::Invalid);
			ok = step("engine", ret);
			vehicle = ret.GetResultData<VehicleID>();
		}
		if (ok && vehicle.has_value()) ok = step("start", Command<Commands::StartStopVehicle>::Do(DoCommandFlag::Execute, *vehicle, false));
		cur_company.Restore();
		if (ok) {
			IConsolePrint(CC_INFO, "Elevated test line at y {} from x {}; train {}.", y, x, vehicle.value_or(VehicleID::Invalid()));
			ScrollMainWindowToTile(t(12), true);
		}
		return true;
	}
	if (cmd == "testspan" && argv.size() > 2) {
		/* For tests: find two free flat tiles in the town with low houses between them and build a span over them. */
		auto id = ParseInteger<uint>(argv[2]);
		const Town *town = id.has_value() ? Town::GetIfValid(*id) : nullptr;
		if (town == nullptr) return false;
		RailType rt = GetRailTypeByLabel(RAILTYPE_LABEL_MONO);
		Backup<CompanyID> cur_company(_current_company, GetConsoleCompany(), FILE_LINE);
		DoCommandFlags test{DoCommandFlag::Bankrupt};
		bool done = false;
		for (TileIndex a : SpiralTileSequence(town->xy, 25)) {
			if (done) break;
			if (!IsTileType(a, TileType::Clear) || GetTileSlope(a) != SLOPE_FLAT) continue;
			for (DiagDirection d : {DiagDirection::SW, DiagDirection::SE}) {
				if (done) break;
				uint houses = 0;
				TileIndex m = a;
				for (uint len = 1; len <= METRO_ELEVATED_MAX_SPAN + 1; len++) {
					m = TileAddByDiagDir(m, d);
					if (!IsValidTile(m)) break;
					if (IsTileType(m, TileType::House)) {
						houses++;
						continue;
					}
					if (houses == 0 || !IsTileType(m, TileType::Clear) || GetTileSlope(m) != SLOPE_FLAT || GetTileZ(m) != GetTileZ(a)) {
						if (!IsTileType(m, TileType::Road)) break;
						continue;
					}
					CommandCost ret = Command<Commands::MetroBuildElevatedSpan>::Do(test, m, a, static_cast<uint8_t>(METRO_ELEVATED_MIN_LEVEL), rt);
					if (ret.Failed()) {
						IConsolePrint(CC_DEFAULT, "span ({}, {})..({}, {}) over {} houses: {}", TileX(a), TileY(a), TileX(m), TileY(m), houses, GetString(ret.GetErrorMessage()));
						break;
					}
					ret = Command<Commands::MetroBuildElevatedSpan>::Do(DoCommandFlags{DoCommandFlag::Execute, DoCommandFlag::Bankrupt}, m, a, static_cast<uint8_t>(METRO_ELEVATED_MIN_LEVEL), rt);
					IConsolePrint(CC_INFO, "span ({}, {})..({}, {}) over {} houses: {}", TileX(a), TileY(a), TileX(m), TileY(m), houses, ret.Succeeded() ? "built" : GetString(ret.GetErrorMessage()));
					if (ret.Succeeded()) {
						ScrollMainWindowToTile(a, true);
						done = true;
					}
					break;
				}
			}
		}
		cur_company.Restore();
		if (!done) IConsolePrint(CC_ERROR, "No span built.");
		return true;
	}
	if (cmd == "trains") {
		for (const Train *v : Train::Iterate()) {
			if (!v->IsFrontEngine()) continue;
			IConsolePrint(CC_DEFAULT, "train {} at ({}, {}) z {} ground z {} speed {}", v->index, TileX(v->tile), TileY(v->tile), v->z_pos, GetTilePixelZ(v->tile), v->cur_speed);
		}
		return true;
	}
	if (cmd == "window") {
		RailType mono = GetRailTypeByLabel(RAILTYPE_LABEL_MONO);
		ShowMetroElevatedToolbar(mono);
		return true;
	}
	if (cmd == "info" && argv.size() > 3) {
		auto t = ParseConsoleTile(argv[2], argv[3]);
		if (!t.has_value()) return false;
		TileIndex tile = *t;
		IConsolePrint(CC_DEFAULT, "level {}, ramp {} (up {}), track z at centre {}, ground z {}", GetMetroElevation(tile), IsMetroRamp(tile) ? "yes" : "no",
				IsMetroRamp(tile) ? static_cast<int>(to_underlying(GetMetroRampUpDir(tile))) : -1,
				GetSlopePixelZ(TileX(tile) * TILE_SIZE + 8, TileY(tile) * TILE_SIZE + 8, true), GetTilePixelZ(tile));
		if (IsBridgeTile(tile)) IConsolePrint(CC_DEFAULT, "bridge height {}", GetBridgeHeight(tile));
		return true;
	}
	if (argv.size() < 6) return false;
	auto a = ParseConsoleTile(argv[2], argv[3]);
	auto b = ParseConsoleTile(argv[4], argv[5]);
	if (!a.has_value() || !b.has_value()) return false;
	if (cmd == "raise" && argv.size() > 6) {
		ConsoleCommand<Commands::MetroElevateRail>(*b, *a, static_cast<uint8_t>(ParseInteger<uint>(argv[6]).value_or(0)));
		return true;
	}
	if (cmd == "line" && argv.size() > 6) {
		ConsoleCommand<Commands::MetroElevateLine>(*b, *a, static_cast<uint8_t>(ParseInteger<uint>(argv[6]).value_or(0)));
		return true;
	}
	if (cmd == "span" && argv.size() > 6) {
		ConsoleCommand<Commands::MetroBuildElevatedSpan>(*b, *a, static_cast<uint8_t>(ParseInteger<uint>(argv[6]).value_or(0)), GetRailTypeByLabel(RAILTYPE_LABEL_MONO));
		return true;
	}
	return false;
}
