/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_power_sl.cpp Metro mod: saving and loading of the power grid (MTPL lines, MTPP plants, MTPC companies). */

#include "../stdafx.h"
#include "../metro_power.h"

#include "saveload.h"

#include "../safeguards.h"

/** A power line tile within the savegame. */
struct MetroPowerLineRecord {
	uint32_t tile = 0;
	uint8_t owner = 0;
};

static const NamedSaveLoad _metro_power_line_desc[] = {
	NSL("tile",  SLE_VAR(MetroPowerLineRecord, tile,  SLE_UINT32)),
	NSL("owner", SLE_VAR(MetroPowerLineRecord, owner, SLE_UINT8)),
};

static const NamedSaveLoad _metro_power_plant_desc[] = {
	NSL("kind",          SLE_VAR(MetroPowerPlantState, kind,          SLE_UINT8)),
	NSL("id",            SLE_VAR(MetroPowerPlantState, id,            SLE_UINT32)),
	NSL("fuel",          SLE_VAR(MetroPowerPlantState, fuel,          SLE_UINT32)),
	NSL("output",        SLE_VAR(MetroPowerPlantState, output,        SLE_UINT16)),
	NSL("burn_progress", SLE_VAR(MetroPowerPlantState, burn_progress, SLE_UINT32)),
	NSL("used_kw",       SLE_VAR(MetroPowerPlantState, used_kw,       SLE_UINT32)),
};

static const NamedSaveLoad _metro_power_company_desc[] = {
	NSL("company",      SLE_VAR(MetroPowerCompanyState, company,      SLE_UINT8)),
	NSL("tracks_first", SLE_VAR(MetroPowerCompanyState, tracks_first, SLE_BOOL)),
};

static void Save_MTPL()
{
	SaveLoadTableData slt = SlTableHeader(_metro_power_line_desc);
	int index = 0;
	for (const auto &[tile, owner] : _metro_power_lines) {
		MetroPowerLineRecord record{tile, owner};
		SlSetArrayIndex(index++);
		SlObjectSaveFiltered(&record, slt);
	}
}

static void Load_MTPL()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_metro_power_line_desc);
	_metro_power_lines.clear();
	while (SlIterateArray() != -1) {
		MetroPowerLineRecord record;
		SlObjectLoadFiltered(&record, slt);
		_metro_power_lines[record.tile] = record.owner;
	}
}

static void Save_MTPP()
{
	SaveLoadTableData slt = SlTableHeader(_metro_power_plant_desc);
	int index = 0;
	for (MetroPowerPlantState &state : _metro_power_plants) {
		SlSetArrayIndex(index++);
		SlObjectSaveFiltered(&state, slt);
	}
}

static void Load_MTPP()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_metro_power_plant_desc);
	_metro_power_plants.clear();
	while (SlIterateArray() != -1) {
		MetroPowerPlantState state;
		SlObjectLoadFiltered(&state, slt);
		_metro_power_plants.push_back(state);
	}
}

static void Save_MTPC()
{
	SaveLoadTableData slt = SlTableHeader(_metro_power_company_desc);
	int index = 0;
	for (auto &[company, state] : _metro_power_companies) {
		SlSetArrayIndex(index++);
		SlObjectSaveFiltered(&state, slt);
	}
}

static void Load_MTPC()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_metro_power_company_desc);
	_metro_power_companies.clear();
	while (SlIterateArray() != -1) {
		MetroPowerCompanyState state;
		SlObjectLoadFiltered(&state, slt);
		_metro_power_companies[state.company] = state;
	}
}

/** Chunk handlers of the metro mod power grid. */
static const ChunkHandler metro_power_chunk_handlers[] = {
	{ 'MTPL', Save_MTPL, Load_MTPL, nullptr, nullptr, ChunkType::Table },
	{ 'MTPP', Save_MTPP, Load_MTPP, nullptr, nullptr, ChunkType::Table },
	{ 'MTPC', Save_MTPC, Load_MTPC, nullptr, nullptr, ChunkType::Table },
};

extern const ChunkHandlerTable _metro_power_chunk_handlers(metro_power_chunk_handlers);
