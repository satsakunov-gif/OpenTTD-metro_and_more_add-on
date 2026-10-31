/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_station_names.cpp Metro mod: names of new stations.
 *
 * A new station of a human player is named after the nearest object within #METRO_STATION_NAME_OBJECT_RADIUS
 * (stadium, university, hospital, town hall), else after its district (business / residential); else it keeps the
 * vanilla name, which already follows the relief (heights, valley, woods, lakeside and so on). A name already used in
 * the town gets the side of the town centre; if that is taken too, the vanilla name stays. The name is stored like a
 * name given by the player, so renaming works as in vanilla.
 */

#include "stdafx.h"
#include "metro_station_names.h"
#include "metro_city.h"
#include "metro_town.h"
#include "company_base.h"
#include "company_func.h"
#include "house.h"
#include "map_func.h"
#include "object_map.h"
#include "station_base.h"
#include "strings_func.h"
#include "tilearea_type.h"
#include "town.h"
#include "town_map.h"
#include "command_func.h"
#include "console_func.h"
#include "console_type.h"
#include "landscape.h"
#include "newgrf_station.h"
#include "station_cmd.h"
#include "station_map.h"
#include "core/backup_type.hpp"
#include "core/string_consumer.hpp"
#include "table/metro_station_names_tables.h"
#include "table/strings.h"

#include "safeguards.h"

/** Object on the tile that names stations, or MetroNamedObject::End. */
static MetroNamedObject GetNamedObjectAt(TileIndex tile, const Town *t)
{
	if (tile == t->xy) return MetroNamedObject::TownHall;
	switch (GetTileType(tile)) {
		case TileType::House:
			if (HouseSpec::Get(GetHouseType(tile))->building_flags.Test(BuildingFlag::IsStadium)) return MetroNamedObject::Stadium;
			break;
		case TileType::Object:
			if (IsMetroServiceObject(GetObjectType(tile))) {
				switch (GetMetroServiceTypeOfObject(GetObjectType(tile))) {
					case MetroServiceType::University: return MetroNamedObject::University;
					case MetroServiceType::Hospital: return MetroNamedObject::Hospital;
					default: break;
				}
			}
			break;
		default:
			break;
	}
	return MetroNamedObject::End;
}

/** Whether another station of the town already has the name. */
static bool IsStationNameUsed(const Station *st, const std::string &name)
{
	for (const Station *s : Station::Iterate()) {
		if (s != st && s->town == st->town && GetString(STR_STATION_NAME, s->index) == name) return true;
	}
	return false;
}

void MetroNameNewStation(Station *st, TileIndex tile)
{
	if (!Company::IsValidHumanID(_current_company) || st->town == nullptr) return;
	const Town *t = st->town;

	/* The nearest object. */
	MetroNamedObject best = MetroNamedObject::End;
	uint best_dist = UINT_MAX;
	for (TileIndex o : SpiralTileSequence(tile, 2 * METRO_STATION_NAME_OBJECT_RADIUS + 1)) {
		MetroNamedObject obj = GetNamedObjectAt(o, t);
		if (obj == MetroNamedObject::End) continue;
		uint dist = DistanceManhattan(o, tile);
		if (dist < best_dist || (dist == best_dist && obj < best)) {
			best = obj;
			best_dist = dist;
		}
	}

	StringID name_id = STR_NULL;
	if (best != MetroNamedObject::End) {
		name_id = _metro_named_object_names[to_underlying(best)];
	} else {
		int district = GetMetroDistrictTypeAt(t, tile);
		if (district >= 0 && district < static_cast<int>(std::size(_metro_district_station_names))) name_id = _metro_district_station_names[district];
	}
	if (name_id == STR_NULL) return;

	std::string name = GetString(name_id, STR_TOWN_NAME, t->index);
	if (IsStationNameUsed(st, name)) {
		/* Clarify by the side of the town centre. */
		int dx = static_cast<int>(TileX(tile)) - static_cast<int>(TileX(t->xy));
		int dy = static_cast<int>(TileY(tile)) - static_cast<int>(TileY(t->xy));
		DiagDirection side = std::abs(dx) >= std::abs(dy) ? (dx < 0 ? DiagDirection::NE : DiagDirection::SW) : (dy < 0 ? DiagDirection::NW : DiagDirection::SE);
		name = GetString(STR_METRO_STNAME_CLARIFIED, name, _metro_station_name_sides[to_underlying(side)]);
		if (IsStationNameUsed(st, name)) return;
	}
	if (Utf8StringLength(name) >= MAX_LENGTH_STATION_NAME_CHARS) return;
	st->name = std::move(name);
}

/* ---------------------------------------------------------------------------------------------- */
/* Console                                                                                        */
/* ---------------------------------------------------------------------------------------------- */

/** Build a one-tile rail station of the first company on a free tile near @p near; prints its name. */
static bool ConsoleBuildStationNear(TileIndex near, uint radius, const char *what)
{
	CompanyID company = Company::IsValidID(_local_company) ? _local_company : CompanyID::Invalid();
	if (company == CompanyID::Invalid()) {
		for (const Company *c : Company::Iterate()) {
			company = c->index;
			break;
		}
	}
	if (company == CompanyID::Invalid()) return false;
	Backup<CompanyID> cur_company(_current_company, company, FILE_LINE);
	bool done = false;
	for (TileIndex t : SpiralTileSequence(near, 2 * radius + 1)) {
		if (!IsTileType(t, TileType::Clear) || GetTileSlope(t) != SLOPE_FLAT) continue;
		bool touching = false;
		for (DiagDirection d : EnumRange(DiagDirection::End)) {
			TileIndex n = TileAddByDiagDir(t, d);
			if (IsValidTile(n) && IsTileType(n, TileType::Station)) touching = true;
			for (DiagDirection d2 : EnumRange(DiagDirection::End)) {
				TileIndex n2 = IsValidTile(n) ? TileAddByDiagDir(n, d2) : INVALID_TILE;
				if (n2 != INVALID_TILE && IsValidTile(n2) && IsTileType(n2, TileType::Station)) touching = true;
			}
		}
		if (touching) continue;
		CommandCost ret = Command<Commands::BuildRailStation>::Do(DoCommandFlags{DoCommandFlag::Execute, DoCommandFlag::Bankrupt}, t, RAILTYPE_RAIL, Axis::X, 1, 1, STAT_CLASS_DFLT, 0, StationID::Invalid(), false);
		if (ret.Failed()) continue;
		const Station *st = Station::GetByTile(t);
		IConsolePrint(CC_INFO, "{} at ({}, {}), {} tiles from the centre of {}: {}", what, TileX(t), TileY(t), DistanceManhattan(t, st->town->xy), GetString(STR_TOWN_NAME, st->town->index), GetString(STR_STATION_NAME, st->index));
		done = true;
		break;
	}
	cur_company.Restore();
	if (!done) IConsolePrint(CC_ERROR, "{}: no free tile.", what);
	return done;
}

bool ConMetroStation(std::span<std::string_view> argv)
{
	if (argv.size() < 3 || argv[1] != "near") {
		IConsolePrint(CC_HELP, "Metro mod station names. Usage: metro_station near <town id> (test stations near the stadium, the town hall and twice in a district)");
		return true;
	}
	auto id = ParseInteger<uint>(argv[2]);
	const Town *t = id.has_value() ? Town::GetIfValid(*id) : nullptr;
	if (t == nullptr) return false;
	for (TileIndex tile : SpiralTileSequence(t->xy, 31)) {
		if (IsTileType(tile, TileType::House) && HouseSpec::Get(GetHouseType(tile))->building_flags.Test(BuildingFlag::IsStadium)) {
			ConsoleBuildStationNear(tile, 3, "near the stadium");
			ConsoleBuildStationNear(tile, 4, "near the stadium again");
			break;
		}
	}
	ConsoleBuildStationNear(t->xy, 3, "near the town hall");
	/* Twice in the same place far from the centre: the second name gets the side. */
	TileIndex far = TileAddWrap(t->xy, 9, 9);
	if (far == INVALID_TILE) far = t->xy;
	ConsoleBuildStationNear(far, 2, "district 1");
	TileIndex far2 = TileAddWrap(far, 0, 6);
	ConsoleBuildStationNear(far2 == INVALID_TILE ? far : far2, 2, "district 2");
	return true;
}
