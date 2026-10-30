/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_city_sl.cpp Metro mod: saving and loading of town states (MTCT).
 *  The fire chunks MTFP, MTFR and MTFT of metro_city v2..v3 saves are skipped: fires were removed from the mod. */

#include "../stdafx.h"
#include "../metro_city.h"

#include "saveload.h"

#include "../safeguards.h"

/** Description of the mod state of a town within the savegame. */
static const NamedSaveLoad _metro_city_town_desc[] = {
	NSL("town",                      SLE_VAR(MetroCityTownState, town,                      SLE_UINT16)),
	NSL("active",                    SLE_VAR(MetroCityTownState, active,                    SLE_BOOL)),
	NSL("active_since",              SLE_VAR(MetroCityTownState, active_since,              SLE_INT32)),
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

/** Fire posts, fires and fire engines of older saves: read and dropped. */
static void Load_MetroFireChunk()
{
	SlSkipChunkContents();
}

/** Chunk handlers of the metro mod towns. */
static const ChunkHandler metro_city_chunk_handlers[] = {
	{ 'MTCT', Save_MTCT, Load_MTCT, nullptr, nullptr, ChunkType::Table },
	{ 'MTFP', nullptr,   Load_MetroFireChunk, nullptr, nullptr, ChunkType::ReadOnly },
	{ 'MTFR', nullptr,   Load_MetroFireChunk, nullptr, nullptr, ChunkType::ReadOnly },
	{ 'MTFT', nullptr,   Load_MetroFireChunk, nullptr, nullptr, ChunkType::ReadOnly },
};

extern const ChunkHandlerTable _metro_city_chunk_handlers(metro_city_chunk_handlers);
