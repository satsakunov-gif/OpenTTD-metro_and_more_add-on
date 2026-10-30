/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_town_sl.cpp Metro mod: saving and loading of the town life (MTDS districts, MTTL town states). */

#include "../stdafx.h"
#include "../metro_town.h"

#include "saveload.h"

#include "../safeguards.h"

static const NamedSaveLoad _metro_district_desc[] = {
	NSL("town", SLE_VAR(MetroDistrict, town, SLE_UINT16)),
	NSL("tile", SLE_VAR(MetroDistrict, tile, SLE_UINT32)),
	NSL("type", SLE_VAR(MetroDistrict, type, SLE_UINT8)),
};

static const NamedSaveLoad _metro_town_life_desc[] = {
	NSL("town",                     SLE_VAR(MetroTownLifeState, town,                     SLE_UINT16)),
	NSL("commuters_month",          SLE_VAR(MetroTownLifeState, commuters_month,          SLE_UINT32)),
	NSL("commuters_last_month",     SLE_VAR(MetroTownLifeState, commuters_last_month,     SLE_UINT32)),
	NSL("commuters_out_month",      SLE_VAR(MetroTownLifeState, commuters_out_month,      SLE_UINT32)),
	NSL("commuters_out_last_month", SLE_VAR(MetroTownLifeState, commuters_out_last_month, SLE_UINT32)),
	NSL("attractiveness",           SLE_VAR(MetroTownLifeState, attractiveness,           SLE_UINT8)),
	NSL("moved_in_year",            SLE_VAR(MetroTownLifeState, moved_in_year,            SLE_UINT32)),
	NSL("moved_out_year",           SLE_VAR(MetroTownLifeState, moved_out_year,           SLE_UINT32)),
	NSL("moved_in_last_year",       SLE_VAR(MetroTownLifeState, moved_in_last_year,       SLE_UINT32)),
	NSL("moved_out_last_year",      SLE_VAR(MetroTownLifeState, moved_out_last_year,      SLE_UINT32)),
	NSL("growth_effect",            SLE_VAR(MetroTownLifeState, growth_effect,            SLE_INT8)),
};

static void Save_MTDS()
{
	SaveLoadTableData slt = SlTableHeader(_metro_district_desc);
	int index = 0;
	for (MetroDistrict &d : _metro_districts) {
		SlSetArrayIndex(index++);
		SlObjectSaveFiltered(&d, slt);
	}
}

static void Load_MTDS()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_metro_district_desc);
	_metro_districts.clear();
	while (SlIterateArray() != -1) {
		MetroDistrict d;
		SlObjectLoadFiltered(&d, slt);
		_metro_districts.push_back(d);
	}
}

static void Save_MTTL()
{
	SaveLoadTableData slt = SlTableHeader(_metro_town_life_desc);
	int index = 0;
	for (auto &[town, state] : _metro_town_life) {
		SlSetArrayIndex(index++);
		SlObjectSaveFiltered(&state, slt);
	}
}

static void Load_MTTL()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_metro_town_life_desc);
	_metro_town_life.clear();
	while (SlIterateArray() != -1) {
		MetroTownLifeState state;
		SlObjectLoadFiltered(&state, slt);
		_metro_town_life[state.town] = state;
	}
}

/** Chunk handlers of the metro mod town life. */
static const ChunkHandler metro_town_chunk_handlers[] = {
	{ 'MTDS', Save_MTDS, Load_MTDS, nullptr, nullptr, ChunkType::Table },
	{ 'MTTL', Save_MTTL, Load_MTTL, nullptr, nullptr, ChunkType::Table },
};

extern const ChunkHandlerTable _metro_town_chunk_handlers(metro_town_chunk_handlers);
