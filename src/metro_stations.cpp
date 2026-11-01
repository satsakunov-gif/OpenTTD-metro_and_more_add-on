/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_stations.cpp Metro mod: underground stations (stage 8, steps 4 and 7).
 *
 * - Levels (#_metro_station_levels): basic, escalators (1975), interchange (1990, needs a second platform, a platform
 *   on another layer or another metro station near), megahub (2030); each bought with a button for a multiple of
 *   the price of the platforms (table/metro_station_tables.h).
 * - Rating: platforms on -2/-3 without escalators, platforms on -3 without a working vent shaft near ("душно"), a
 *   bonus for every entrance after the first (#MetroStationRatingModifier, called from GetTargetRating).
 * - Entrances besides the pavilion object (#_metro_extra_entrances): on the sidewalk of a road tile (the road stays
 *   free; platforms of -2/-3) and in the basement of a house of the town (platforms of -3, expensive). An entrance
 *   leads to the platforms within 3 tiles, widens the catchment and joins the platform to a station it touches.
 */

#include "stdafx.h"
#include "metro_heatmap.h"
#include "metro_malls.h"
#include "metro_stations.h"
#include "metro_underground.h"
#include "metro_underground_cmd.h"
#include "metro_overlay.h"
#include "metro_power.h"
#include "station_base.h"
#include "station_map.h"
#include "object_base.h"
#include "object_map.h"
#include "road_map.h"
#include "town_map.h"
#include "company_base.h"
#include "company_func.h"
#include "console_func.h"
#include "console_type.h"
#include "command_func.h"
#include "date_func.h"
#include "economy_func.h"
#include "strings_func.h"
#include "viewport_func.h"
#include "window_func.h"
#include "sprite.h"
#include "tile_cmd.h"
#include "core/enum_type.hpp"
#include "table/strings.h"
#include "table/sprites.h"
#include "table/metro_underground_tables.h"
#include "table/metro_station_tables.h"

#include "safeguards.h"

std::map<uint16_t, uint8_t> _metro_station_levels;
std::map<uint32_t, MetroExtraEntrance> _metro_extra_entrances;

void MetroStationsReset()
{
	_metro_station_levels.clear();
	_metro_extra_entrances.clear();
	MetroMallsReset();
	MetroHeatReset();
}

uint8_t GetMetroStationLevel(StationID station)
{
	auto it = _metro_station_levels.find(station.base());
	return it == _metro_station_levels.end() ? 0 : it->second;
}

void MetroStationsOnStationDeleted(StationID station)
{
	_metro_station_levels.erase(station.base());
	MetroMallsOnStationDeleted(station);
	MetroHeatOnStationDeleted(station);
}

/** Chebyshev distance between two tile areas (0 if they touch or overlap). */
static uint AreaDistance(const TileArea &a, const TileArea &b)
{
	int ax = TileX(a.tile), ay = TileY(a.tile), bx = TileX(b.tile), by = TileY(b.tile);
	int dx = std::max({0, ax - (bx + b.w - 1), bx - (ax + a.w - 1)});
	int dy = std::max({0, ay - (by + b.h - 1), by - (ay + a.h - 1)});
	return static_cast<uint>(std::max(dx, dy));
}

/** Whether the entrance on a road or in a house still stands (the road or the house may be gone). */
static bool IsExtraEntranceValid(TileIndex tile, const MetroExtraEntrance &e)
{
	if (e.kind == MetroEntranceKind::Sidewalk) return IsTileType(tile, TileType::Road) && IsNormalRoad(tile);
	return IsTileType(tile, TileType::House);
}

/** Whether an entrance of the company (pavilion, sidewalk or basement) is on the tile. */
bool IsMetroEntranceTileOf(TileIndex tile, Owner owner)
{
	if (IsTileType(tile, TileType::Object) && IsMetroEntranceObject(GetObjectType(tile)) && GetTileOwner(tile) == owner) {
		return Object::GetByTile(tile)->view == 0; // A transformer portal is no entrance.
	}
	auto it = _metro_extra_entrances.find(tile.base());
	return it != _metro_extra_entrances.end() && Owner{it->second.owner} == owner && IsExtraEntranceValid(tile, it->second);
}

/** Call @p f for every entrance of the company within @p radius of the area. */
void MetroForEachEntranceNear(const TileArea &area, uint radius, Owner owner, const std::function<void(TileIndex)> &f)
{
	TileArea around = area;
	around.Expand(radius);
	for (TileIndex t : around) {
		if (IsMetroEntranceTileOf(t, owner)) f(t);
	}
}

/** The platforms of a station under the ground. */
struct MetroPlatformInfo {
	uint tiles = 0;     ///< Platform tiles.
	uint8_t layers = 0; ///< Bit l-1 for platforms on layer -l.
	Money price = 0;    ///< What the platforms cost to build now.
};

static MetroPlatformInfo GetPlatformInfo(const Station *st)
{
	MetroPlatformInfo pi;
	if (_metro_overlay.empty() || st->train_station.IsEmpty()) return pi;
	for (TileIndex t : st->train_station) {
		if (MetroOverlayStationIndex(t) != st->index) continue;
		for (uint l = 1; l <= METRO_LAYER_COUNT; l++) {
			const MetroUndergroundTile *ut = GetMetroUndergroundTile(t, l);
			if (ut == nullptr || ut->depot != METRO_UNDERGROUND_PLATFORM) continue;
			pi.tiles++;
			SetBit(pi.layers, l - 1);
			pi.price += MetroGetUndergroundPiecePrice(l) * 3 + _price[Price::BuildStationRail];
			break;
		}
	}
	return pi;
}

/** Layers (bits) of the platforms of the company within the entrance radius of the tile. */
static uint8_t GetPlatformLayersNear(TileIndex tile, Owner owner)
{
	uint8_t layers = 0;
	TileArea around(tile, 1, 1);
	around.Expand(METRO_ENTRANCE_PLATFORM_RADIUS);
	for (TileIndex t : around) {
		for (uint l = 1; l <= METRO_LAYER_COUNT; l++) {
			const MetroUndergroundTile *ut = GetMetroUndergroundTile(t, l);
			if (ut != nullptr && ut->depot == METRO_UNDERGROUND_PLATFORM && Owner{ut->owner} == owner) SetBit(layers, l - 1);
		}
	}
	return layers;
}

static uint CountEntrances(const Station *st)
{
	uint n = 0;
	MetroForEachEntranceNear(st->train_station, METRO_ENTRANCE_PLATFORM_RADIUS, st->owner, [&](TileIndex) { n++; });
	return n;
}

/** Rating points for a percentage of the full rating. */
static int RatingPercent(int percent)
{
	return 255 * percent / 100;
}

/** Change of the station rating from the metro (0 for other stations). */
int MetroStationRatingModifier(const Station *st)
{
	if (_metro_overlay.empty() || st->train_station.IsEmpty()) return 0;
	MetroPlatformInfo pi = GetPlatformInfo(st);
	if (pi.tiles == 0) return 0;
	int mod = 0;
	if ((pi.layers & 0b110) != 0 && GetMetroStationLevel(st->index) < METRO_STATION_LEVEL_ESCALATORS) mod -= RatingPercent(METRO_RATING_NO_ESCALATOR_PERCENT);
	if (HasBit(pi.layers, 2) && !MetroPowerHasWorkingVentNear(st->train_station, METRO_VENT_RADIUS, st->owner)) mod -= RatingPercent(METRO_RATING_STUFFY_PERCENT);
	uint entrances = CountEntrances(st);
	if (entrances > 1) mod += RatingPercent(std::min<int>((entrances - 1) * METRO_RATING_ENTRANCE_PERCENT, METRO_RATING_ENTRANCE_MAX_PERCENT));
	return mod;
}

/** A station of the company that an entrance near the platforms touches; the platforms join it (as in vanilla). */
StationID MetroFindEntranceJoinStation(const TileArea &platforms, Owner owner)
{
	StationID found = StationID::Invalid();
	MetroForEachEntranceNear(platforms, METRO_ENTRANCE_PLATFORM_RADIUS, owner, [&](TileIndex e) {
		if (found != StationID::Invalid()) return;
		for (DiagDirection d : EnumRange(DiagDirection::End)) {
			TileIndex n = TileAddByDiagDir(e, d);
			if (!IsValidTile(n) || !IsTileType(n, TileType::Station) || GetTileOwner(n) != owner) continue;
			const Station *st = Station::GetIfValid(GetStationIndex(n));
			if (st == nullptr) continue;
			/* A railway station of the surface stays apart: the worlds join only at the transformer. */
			if (!st->train_station.IsEmpty() && GetPlatformInfo(st).tiles == 0) continue;
			found = st->index;
			return;
		}
	});
	return found;
}

/** Recompute the catchment of the underground stations an entrance on the tile serves. */
void MetroStationsRecomputeNear(TileIndex tile)
{
	TileArea at(tile, 1, 1);
	for (Station *st : Station::Iterate()) {
		if (st->train_station.IsEmpty() || AreaDistance(st->train_station, at) > METRO_ENTRANCE_PLATFORM_RADIUS) continue;
		if (GetPlatformInfo(st).tiles == 0) continue;
		st->RecomputeCatchment();
	}
}

/** Whether the station may become an interchange: a second platform, platforms on two layers or another metro station near. */
static bool CanBeInterchange(const Station *st, const MetroPlatformInfo &pi)
{
	if (CountBits(pi.layers) >= 2) return true;
	if (st->train_station.w > 1 && st->train_station.h > 1) return true; // Platforms are one track wide: two tracks side by side.
	for (const Station *o : Station::Iterate()) {
		if (o == st || o->owner != st->owner || o->train_station.IsEmpty()) continue;
		if (AreaDistance(st->train_station, o->train_station) > METRO_INTERCHANGE_RADIUS) continue;
		if (GetPlatformInfo(o).tiles != 0) return true;
	}
	return false;
}

/**
 * Upgrade an underground station to its next level.
 * @param flags type of operation
 * @param tile a platform tile of the station
 */
CommandCost CmdMetroUpgradeStation(DoCommandFlags flags, TileIndex tile)
{
	if (!Company::IsValidID(_current_company)) return CMD_ERROR;
	Station *st = Station::GetIfValid(MetroOverlayStationIndex(tile));
	if (st == nullptr || st->owner != _current_company) return CommandCost(STR_METRO_STATION_ERR_NOT_STATION);
	MetroPlatformInfo pi = GetPlatformInfo(st);
	if (pi.tiles == 0) return CommandCost(STR_METRO_STATION_ERR_NOT_STATION);
	uint8_t level = GetMetroStationLevel(st->index);
	if (level + 1u >= std::size(_metro_station_levels_spec)) return CommandCost(STR_METRO_STATION_ERR_MAX_LEVEL);
	const MetroStationLevelSpec &next = _metro_station_levels_spec[level + 1];
	if (CalTime::CurYear().base() < next.year) return CommandCostWithParam(STR_METRO_STATION_ERR_LEVEL_YEAR, next.year);
	if (level + 1 == METRO_STATION_LEVEL_INTERCHANGE && !CanBeInterchange(st, pi)) return CommandCost(STR_METRO_STATION_ERR_NO_INTERCHANGE);

	CommandCost cost(ExpensesType::Construction, pi.price * next.cost_factor);
	if (flags.Test(DoCommandFlag::Execute)) {
		_metro_station_levels[st->index.base()] = level + 1;
		SetWindowDirty(WindowClass::StationView, st->index);
	}
	return cost;
}

/**
 * Build or remove an entrance on the sidewalk of a road tile or in the basement of a house.
 * @param flags type of operation
 * @param tile the road or house tile
 * @param remove remove the entrance instead
 */
CommandCost CmdMetroBuildExtraEntrance(DoCommandFlags flags, TileIndex tile, bool remove)
{
	if (!Company::IsValidID(_current_company)) return CMD_ERROR;
	auto it = _metro_extra_entrances.find(tile.base());
	if (remove) {
		if (it == _metro_extra_entrances.end() || Owner{it->second.owner} != _current_company) return CommandCost(STR_METRO_STATION_ERR_NO_ENTRANCE);
		if (flags.Test(DoCommandFlag::Execute)) {
			_metro_extra_entrances.erase(it);
			MarkTileDirtyByTile(tile);
			MetroStationsRecomputeNear(tile);
		}
		return CommandCost(ExpensesType::Construction, _price[Price::ClearRough]);
	}
	if (it != _metro_extra_entrances.end() && IsExtraEntranceValid(tile, it->second)) return CommandCost(STR_METRO_STATION_ERR_ENTRANCE_EXISTS);

	MetroEntranceKind kind;
	if (IsTileType(tile, TileType::Road) && IsNormalRoad(tile)) {
		kind = MetroEntranceKind::Sidewalk;
	} else if (IsTileType(tile, TileType::House)) {
		kind = MetroEntranceKind::Basement;
	} else {
		return CommandCost(STR_METRO_STATION_ERR_EXTRA_PLACE);
	}
	uint8_t layers = GetPlatformLayersNear(tile, _current_company);
	Money price = _price[Price::BuildRail] * METRO_ENTRANCE_COST_FACTOR;
	if (kind == MetroEntranceKind::Sidewalk) {
		if (GB(layers, METRO_SIDEWALK_MIN_LAYER - 1, METRO_LAYER_COUNT) == 0) return CommandCost(STR_METRO_STATION_ERR_SIDEWALK_LAYER);
	} else {
		if (!HasBit(layers, METRO_BASEMENT_LAYER - 1)) return CommandCost(STR_METRO_STATION_ERR_BASEMENT_LAYER);
		price *= METRO_BASEMENT_COST_MULTIPLIER;
	}
	if (flags.Test(DoCommandFlag::Execute)) {
		_metro_extra_entrances[tile.base()] = {_current_company.base(), kind};
		MarkTileDirtyByTile(tile);
		MetroStationsRecomputeNear(tile);
	}
	return CommandCost(ExpensesType::Construction, price);
}

/** Draw an entrance on a road or in a house (a vanilla post in the company colour for now). */
void MetroStationsDrawTile(const TileInfo *ti)
{
	if (_metro_extra_entrances.empty()) return;
	auto it = _metro_extra_entrances.find(ti->tile.base());
	if (it == _metro_extra_entrances.end() || !IsExtraEntranceValid(ti->tile, it->second)) return;
	int8_t x = it->second.kind == MetroEntranceKind::Sidewalk ? 1 : 12;
	AddSortableSpriteToDraw(SPR_TRANSMITTER, GetCompanyPalette(static_cast<Owner>(it->second.owner)), ti->x, ti->y, ti->z, {{x, 1, 0}, {2, 2, 70}, {}});
}

/** Whether the station has platforms under the ground. */
bool MetroStationIsUnderground(const Station *st)
{
	return GetPlatformInfo(st).tiles != 0;
}

/** Price of the upgrade of the station to a level. */
Money MetroStationLevelPrice(const Station *st, uint8_t level)
{
	return GetPlatformInfo(st).price * _metro_station_levels_spec[level].cost_factor;
}

bool MetroStationCanBeInterchange(const Station *st)
{
	return CanBeInterchange(st, GetPlatformInfo(st));
}

/** Console: the underground stations with level, platforms, entrances and the rating change. */
void MetroStationsConsoleList()
{
	for (const Station *st : Station::Iterate()) {
		MetroPlatformInfo pi = GetPlatformInfo(st);
		if (pi.tiles == 0) continue;
		IConsolePrint(CC_INFO, "station {} '{}': level {} ({}), platforms {} on layers 0x{:X}, entrances {}, interchange possible {}, working vent near {}, rating change {}",
				st->index, GetString(STR_STATION_NAME, st->index), GetMetroStationLevel(st->index), GetString(_metro_station_levels_spec[GetMetroStationLevel(st->index)].name),
				pi.tiles, pi.layers, CountEntrances(st), CanBeInterchange(st, pi), MetroPowerHasWorkingVentNear(st->train_station, METRO_VENT_RADIUS, st->owner),
				MetroStationRatingModifier(st));
	}
	for (const auto &[tile, e] : _metro_extra_entrances) {
		IConsolePrint(CC_DEFAULT, "entrance {} at ({}, {}), company {}{}", e.kind == MetroEntranceKind::Sidewalk ? "sidewalk" : "basement", TileX(TileIndex{tile}), TileY(TileIndex{tile}), e.owner + 1,
				IsExtraEntranceValid(TileIndex{tile}, e) ? "" : " (gone)");
	}
}
