/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_city_sl.cpp Metro mod: saving and loading of town states (MTCT), fire posts (MTFP), fires (MTFR) and fire engines (MTFT). */

#include "../stdafx.h"
#include "../metro_city.h"

#include "saveload.h"

#include "../safeguards.h"

/** Description of the mod state of a town within the savegame. */
static const NamedSaveLoad _metro_city_town_desc[] = {
	NSL("town",                      SLE_VAR(MetroCityTownState, town,                      SLE_UINT16)),
	NSL("active",                    SLE_VAR(MetroCityTownState, active,                    SLE_BOOL)),
	NSL("active_since",              SLE_VAR(MetroCityTownState, active_since,              SLE_INT32)),
	NSL("fires_put_out",             SLE_VAR(MetroCityTownState, fires_put_out,             SLE_UINT16)),
	NSL("houses_burned",             SLE_VAR(MetroCityTownState, houses_burned,             SLE_UINT16)),
	NSL("tourists_month",            SLE_VAR(MetroCityTownState, tourists_month,            SLE_UINT32)),
	NSL("tourists_last_month",       SLE_VAR(MetroCityTownState, tourists_last_month,       SLE_UINT32)),
	NSL("tourist_income_month",      SLE_VAR(MetroCityTownState, tourist_income_month,      SLE_INT64)),
	NSL("tourist_income_last_month", SLE_VAR(MetroCityTownState, tourist_income_last_month, SLE_INT64)),
	NSL("next_match",                SLE_VAR(MetroCityTownState, next_match,                SLE_INT32)),
	NSL("match_level",               SLE_VAR(MetroCityTownState, match_level,               SLE_UINT8)),
	NSL("match_announced",           SLE_VAR(MetroCityTownState, match_announced,           SLE_BOOL)),
	NSL("match_day",                 SLE_VAR(MetroCityTownState, match_day,                 SLE_INT32)),
	NSL("match_day_level",           SLE_VAR(MetroCityTownState, match_day_level,           SLE_UINT8)),
};

static void Save_MTCT()
{
	SaveLoadTableData slt = SlTableHeader(_metro_city_town_desc);

	int index = 0;
	for (auto &[town, state] : _metro_city_towns) {
		SlSetArrayIndex(index++);
		SlObjectSaveFiltered(&state, slt);
	}
}

static void Load_MTCT()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_metro_city_town_desc);

	_metro_city_towns.clear();
	while (SlIterateArray() != -1) {
		MetroCityTownState state;
		SlObjectLoadFiltered(&state, slt);
		_metro_city_towns[state.town] = state;
	}
}

/** Description of a fire post within the savegame. */
static const NamedSaveLoad _metro_fire_post_desc[] = {
	NSL("tile",       SLE_VAR(MetroFirePost, tile,       SLE_UINT32)),
	NSL("town",       SLE_VAR(MetroFirePost, town,       SLE_UINT16)),
	NSL("owner",      SLE_VAR(MetroFirePost, owner,      SLE_UINT8)),
	NSL("build_date", SLE_VAR(MetroFirePost, build_date, SLE_INT32)),
};

/** Description of a fire within the savegame. */
static const NamedSaveLoad _metro_fire_desc[] = {
	NSL("tile",       SLE_VAR(MetroFire, tile,       SLE_UINT32)),
	NSL("town",       SLE_VAR(MetroFire, town,       SLE_UINT16)),
	NSL("ticks_left", SLE_VAR(MetroFire, ticks_left, SLE_UINT32)),
	NSL("controlled", SLE_VAR(MetroFire, controlled, SLE_BOOL)),
};

/** Description of a fire engine within the savegame. */
static const NamedSaveLoad _metro_fire_truck_desc[] = {
	NSL("home",     SLE_VAR(MetroFireTruck, home,     SLE_UINT32)),
	NSL("fire",     SLE_VAR(MetroFireTruck, fire,     SLE_UINT32)),
	NSL("pos",      SLE_VAR(MetroFireTruck, pos,      SLE_UINT32)),
	NSL("state",    SLE_VAR(MetroFireTruck, state,    SLE_UINT8)),
	NSL("progress", SLE_VAR(MetroFireTruck, progress, SLE_UINT16)),
	NSL("timer",    SLE_VAR(MetroFireTruck, timer,    SLE_UINT16)),
};

/** Save a vector of plain records. */
template <typename T>
static void SaveVector(std::vector<T> &items, std::span<const NamedSaveLoad> desc)
{
	SaveLoadTableData slt = SlTableHeader(desc);

	int index = 0;
	for (T &item : items) {
		SlSetArrayIndex(index++);
		SlObjectSaveFiltered(&item, slt);
	}
}

/** Load a vector of plain records. */
template <typename T>
static void LoadVector(std::vector<T> &items, std::span<const NamedSaveLoad> desc)
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(desc);

	items.clear();
	while (SlIterateArray() != -1) {
		SlObjectLoadFiltered(&items.emplace_back(), slt);
	}
}

static void Save_MTFP() { SaveVector(_metro_fire_posts, _metro_fire_post_desc); }
static void Load_MTFP() { LoadVector(_metro_fire_posts, _metro_fire_post_desc); }
static void Save_MTFR() { SaveVector(_metro_fires, _metro_fire_desc); }
static void Load_MTFR() { LoadVector(_metro_fires, _metro_fire_desc); }
static void Save_MTFT() { SaveVector(_metro_fire_trucks, _metro_fire_truck_desc); }
static void Load_MTFT() { LoadVector(_metro_fire_trucks, _metro_fire_truck_desc); }

/** Chunk handlers of the metro mod towns. */
static const ChunkHandler metro_city_chunk_handlers[] = {
	{ 'MTCT', Save_MTCT, Load_MTCT, nullptr, nullptr, ChunkType::Table },
	{ 'MTFP', Save_MTFP, Load_MTFP, nullptr, nullptr, ChunkType::Table },
	{ 'MTFR', Save_MTFR, Load_MTFR, nullptr, nullptr, ChunkType::Table },
	{ 'MTFT', Save_MTFT, Load_MTFT, nullptr, nullptr, ChunkType::Table },
};

extern const ChunkHandlerTable _metro_city_chunk_handlers(metro_city_chunk_handlers);
