/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_civic_sl.cpp Metro mod: saving and loading of the government contracts (MTGC, MTGN) and the mayor
 * elections (MTEL towns, MTEC candidates, MTEH results).
 */

#include "../stdafx.h"
#include "../metro_civic.h"

#include "saveload.h"

#include "../safeguards.h"

static const NamedSaveLoad _metro_contract_desc[] = {
	NSL("id",         SLE_VAR(MetroContract, id,         SLE_UINT32)),
	NSL("town",       SLE_VAR(MetroContract, town,       SLE_UINT16)),
	NSL("type",       SLE_VAR(MetroContract, type,       SLE_UINT8)),
	NSL("state",      SLE_VAR(MetroContract, state,      SLE_UINT8)),
	NSL("offer_date", SLE_VAR(MetroContract, offer_date, SLE_INT32)),
	NSL("deadline",   SLE_VAR(MetroContract, deadline,   SLE_INT32)),
	NSL("end_date",   SLE_VAR(MetroContract, end_date,   SLE_INT32)),
	NSL("target",     SLE_VAR(MetroContract, target,     SLE_UINT32)),
	NSL("progress",   SLE_VAR(MetroContract, progress,   SLE_UINT8)),
	NSL("last_value", SLE_VAR(MetroContract, last_value, SLE_UINT32)),
	NSL("reward",     SLE_VAR(MetroContract, reward,     SLE_INT64)),
	NSL("rating",     SLE_VAR(MetroContract, rating,     SLE_INT16)),
	NSL("title",      SLE_SSTR(MetroContract, title,     SLE_STR | SLF_ALLOW_CONTROL | SLF_ALLOW_NEWLINE)),
	NSL("text",       SLE_SSTR(MetroContract, text,      SLE_STR | SLF_ALLOW_CONTROL | SLF_ALLOW_NEWLINE)),
};

static void Save_MTGC()
{
	SaveLoadTableData slt = SlTableHeader(_metro_contract_desc);
	int index = 0;
	for (MetroContract &c : _metro_contracts) {
		SlSetArrayIndex(index++);
		SlObjectSaveFiltered(&c, slt);
	}
}

static void Load_MTGC()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_metro_contract_desc);
	_metro_contracts.clear();
	while (SlIterateArray() != -1) SlObjectLoadFiltered(&_metro_contracts.emplace_back(), slt);
}

/** Next offer of a town in the save. */
struct MetroContractTownRecord {
	uint16_t town = 0;
	int32_t next_offer = 0;
};

static const NamedSaveLoad _metro_contract_town_desc[] = {
	NSL("town",       SLE_VAR(MetroContractTownRecord, town,       SLE_UINT16)),
	NSL("next_offer", SLE_VAR(MetroContractTownRecord, next_offer, SLE_INT32)),
};

static void Save_MTGN()
{
	SaveLoadTableData slt = SlTableHeader(_metro_contract_town_desc);
	int index = 0;
	for (const auto &[town, date] : _metro_contract_next_offer) {
		MetroContractTownRecord r{town, date};
		SlSetArrayIndex(index++);
		SlObjectSaveFiltered(&r, slt);
	}
}

static void Load_MTGN()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_metro_contract_town_desc);
	_metro_contract_next_offer.clear();
	while (SlIterateArray() != -1) {
		MetroContractTownRecord r;
		SlObjectLoadFiltered(&r, slt);
		_metro_contract_next_offer[r.town] = r.next_offer;
	}
}

static const NamedSaveLoad _metro_elections_desc[] = {
	NSL("town",             SLE_VAR(MetroTownElections, town,             SLE_UINT16)),
	NSL("next_date",        SLE_VAR(MetroTownElections, next_date,        SLE_INT32)),
	NSL("announced",        SLE_VAR(MetroTownElections, announced,        SLE_BOOL)),
	NSL("last_advertising", SLE_VAR(MetroTownElections, last_advertising, SLE_INT32)),
};

static void Save_MTEL()
{
	SaveLoadTableData slt = SlTableHeader(_metro_elections_desc);
	int index = 0;
	for (auto &[town, e] : _metro_elections) {
		SlSetArrayIndex(index++);
		SlObjectSaveFiltered(&e, slt);
	}
}

static void Load_MTEL()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_metro_elections_desc);
	_metro_elections.clear();
	while (SlIterateArray() != -1) {
		MetroTownElections e;
		SlObjectLoadFiltered(&e, slt);
		_metro_elections[e.town] = std::move(e);
	}
}

/** A candidate in the save, with its town. */
struct MetroCandidateRecord {
	uint16_t town = 0;
	MetroElectionCandidate c;
};

static const NamedSaveLoad _metro_candidate_desc[] = {
	NSL("town",      SLE_VAR(MetroCandidateRecord, town,        SLE_UINT16)),
	NSL("name",      SLE_SSTR(MetroCandidateRecord, c.name,     SLE_STR | SLF_ALLOW_CONTROL)),
	NSL("character", SLE_VAR(MetroCandidateRecord, c.character, SLE_UINT8)),
	NSL("loyal",     SLE_VAR(MetroCandidateRecord, c.loyal,     SLE_BOOL)),
	NSL("program",   SLE_SSTR(MetroCandidateRecord, c.program,  SLE_STR | SLF_ALLOW_CONTROL | SLF_ALLOW_NEWLINE)),
};

static void Save_MTEC()
{
	SaveLoadTableData slt = SlTableHeader(_metro_candidate_desc);
	int index = 0;
	for (const auto &[town, e] : _metro_elections) {
		for (const MetroElectionCandidate &c : e.candidates) {
			MetroCandidateRecord r{town, c};
			SlSetArrayIndex(index++);
			SlObjectSaveFiltered(&r, slt);
		}
	}
}

static void Load_MTEC()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_metro_candidate_desc);
	while (SlIterateArray() != -1) {
		MetroCandidateRecord r;
		SlObjectLoadFiltered(&r, slt);
		auto it = _metro_elections.find(r.town);
		if (it != _metro_elections.end()) it->second.candidates.push_back(std::move(r.c));
	}
}

/** A past election in the save, with its town. */
struct MetroResultRecord {
	uint16_t town = 0;
	MetroElectionResult r;
};

static const NamedSaveLoad _metro_result_desc[] = {
	NSL("town",      SLE_VAR(MetroResultRecord, town,        SLE_UINT16)),
	NSL("date",      SLE_VAR(MetroResultRecord, r.date,      SLE_INT32)),
	NSL("winner",    SLE_SSTR(MetroResultRecord, r.winner,   SLE_STR | SLF_ALLOW_CONTROL)),
	NSL("character", SLE_VAR(MetroResultRecord, r.character, SLE_UINT8)),
	NSL("loyal",     SLE_VAR(MetroResultRecord, r.loyal,     SLE_BOOL)),
	NSL("delta",     SLE_VAR(MetroResultRecord, r.delta,     SLE_INT16)),
	NSL("chance",    SLE_VAR(MetroResultRecord, r.chance,    SLE_UINT8)),
};

static void Save_MTEH()
{
	SaveLoadTableData slt = SlTableHeader(_metro_result_desc);
	int index = 0;
	for (const auto &[town, e] : _metro_elections) {
		for (const MetroElectionResult &res : e.history) {
			MetroResultRecord r{town, res};
			SlSetArrayIndex(index++);
			SlObjectSaveFiltered(&r, slt);
		}
	}
}

static void Load_MTEH()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_metro_result_desc);
	while (SlIterateArray() != -1) {
		MetroResultRecord r;
		SlObjectLoadFiltered(&r, slt);
		auto it = _metro_elections.find(r.town);
		if (it != _metro_elections.end()) it->second.history.push_back(std::move(r.r));
	}
}

/** Chunk handlers of the metro mod contracts and elections; MTEL must come before MTEC and MTEH. */
static const ChunkHandler metro_civic_chunk_handlers[] = {
	{ 'MTGC', Save_MTGC, Load_MTGC, nullptr, nullptr, ChunkType::Table },
	{ 'MTGN', Save_MTGN, Load_MTGN, nullptr, nullptr, ChunkType::Table },
	{ 'MTEL', Save_MTEL, Load_MTEL, nullptr, nullptr, ChunkType::Table },
	{ 'MTEC', Save_MTEC, Load_MTEC, nullptr, nullptr, ChunkType::Table },
	{ 'MTEH', Save_MTEH, Load_MTEH, nullptr, nullptr, ChunkType::Table },
};

extern const ChunkHandlerTable _metro_civic_chunk_handlers(metro_civic_chunk_handlers);
