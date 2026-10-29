/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_ai_sl.cpp Metro mod: saving and loading of the AI town reviews (MTAI) and yearly reports (MTYR). */

#include "../stdafx.h"
#include "../metro_ai.h"

#include "saveload.h"

#include "../safeguards.h"

/** Description of the AI state of a town within the savegame. */
static const NamedSaveLoad _metro_town_ai_desc[] = {
	NSL("town",               SLE_VAR(MetroTownAiState, town,               SLE_UINT16)),
	NSL("mayor",              SLE_VAR(MetroTownAiState, mayor,              SLE_UINT8)),
	NSL("has_review",         SLE_VAR(MetroTownAiState, has_review,         SLE_BOOL)),
	NSL("ai_rating",          SLE_VAR(MetroTownAiState, ai_rating,          SLE_INT16)),
	NSL("last_delta",         SLE_VAR(MetroTownAiState, last_delta,         SLE_INT16)),
	NSL("review_date",        SLE_VAR(MetroTownAiState, review_date,        SLE_INT32)),
	NSL("verdict",            SLE_SSTR(MetroTownAiState, verdict,           SLE_STR | SLF_ALLOW_CONTROL | SLF_ALLOW_NEWLINE)),
	NSL("explanation",        SLE_SSTR(MetroTownAiState, explanation,       SLE_STR | SLF_ALLOW_CONTROL | SLF_ALLOW_NEWLINE)),
	NSL("needs",              SLE_SSTR(MetroTownAiState, needs,             SLE_STR | SLF_ALLOW_CONTROL | SLF_ALLOW_NEWLINE)),
	NSL("changes",            SLE_ARR(MetroTownAiState, changes,            SLE_UINT16, METRO_TOWN_CHANGE_COUNT)),
	NSL("last_population",    SLE_VAR(MetroTownAiState, last_population,    SLE_UINT32)),
	NSL("last_station_count", SLE_VAR(MetroTownAiState, last_station_count, SLE_UINT16)),
};

static void Save_MTAI()
{
	SaveLoadTableData slt = SlTableHeader(_metro_town_ai_desc);

	int index = 0;
	for (auto &[town, state] : _metro_town_ai) {
		SlSetArrayIndex(index++);
		SlObjectSaveFiltered(&state, slt);
	}
}

static void Load_MTAI()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_metro_town_ai_desc);

	_metro_town_ai.clear();
	while (SlIterateArray() != -1) {
		MetroTownAiState state;
		SlObjectLoadFiltered(&state, slt);
		_metro_town_ai[state.town] = std::move(state);
	}
}

/** Description of a yearly report within the savegame. */
static const NamedSaveLoad _metro_year_report_desc[] = {
	NSL("year",  SLE_VAR(MetroYearReport, year,   SLE_INT32)),
	NSL("title", SLE_SSTR(MetroYearReport, title, SLE_STR | SLF_ALLOW_CONTROL | SLF_ALLOW_NEWLINE)),
	NSL("text",  SLE_SSTR(MetroYearReport, text,  SLE_STR | SLF_ALLOW_CONTROL | SLF_ALLOW_NEWLINE)),
};

static void Save_MTYR()
{
	SaveLoadTableData slt = SlTableHeader(_metro_year_report_desc);

	int index = 0;
	for (MetroYearReport &report : _metro_year_reports) {
		SlSetArrayIndex(index++);
		SlObjectSaveFiltered(&report, slt);
	}
}

static void Load_MTYR()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_metro_year_report_desc);

	_metro_year_reports.clear();
	while (SlIterateArray() != -1) {
		SlObjectLoadFiltered(&_metro_year_reports.emplace_back(), slt);
	}
}

/** Chunk handlers of the metro mod AI. */
static const ChunkHandler metro_ai_chunk_handlers[] = {
	{ 'MTAI', Save_MTAI, Load_MTAI, nullptr, nullptr, ChunkType::Table },
	{ 'MTYR', Save_MTYR, Load_MTYR, nullptr, nullptr, ChunkType::Table },
};

extern const ChunkHandlerTable _metro_ai_chunk_handlers(metro_ai_chunk_handlers);
