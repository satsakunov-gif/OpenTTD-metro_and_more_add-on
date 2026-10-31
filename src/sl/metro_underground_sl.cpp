/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_underground_sl.cpp Code handling saving and loading of the metro mod underground layers. */

#include "../stdafx.h"
#include "../metro_underground.h"

#include "saveload.h"

#include "../safeguards.h"

/** One underground tile in the save. */
struct MetroUndergroundRecord {
	uint32_t tile = 0;
	uint8_t layer = 0;
	uint8_t tracks = 0;
	uint8_t owner = 0;
};

static const NamedSaveLoad _metro_underground_desc[] = {
	NSL("tile",   SLE_VAR(MetroUndergroundRecord, tile,   SLE_UINT32)),
	NSL("layer",  SLE_VAR(MetroUndergroundRecord, layer,  SLE_UINT8)),
	NSL("tracks", SLE_VAR(MetroUndergroundRecord, tracks, SLE_UINT8)),
	NSL("owner",  SLE_VAR(MetroUndergroundRecord, owner,  SLE_UINT8)),
};

static void Save_MTUG()
{
	SaveLoadTableData slt = SlTableHeader(_metro_underground_desc);
	int index = 0;
	for (const auto &[key, ut] : _metro_underground) {
		MetroUndergroundRecord record{static_cast<uint32_t>(key >> 2), static_cast<uint8_t>(key & 3), ut.tracks, ut.owner};
		SlSetArrayIndex(index++);
		SlObjectSaveFiltered(&record, slt);
	}
}

static void Load_MTUG()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_metro_underground_desc);
	_metro_underground.clear();
	while (SlIterateArray() != -1) {
		MetroUndergroundRecord record;
		SlObjectLoadFiltered(&record, slt);
		_metro_underground[MetroUndergroundKey(TileIndex{record.tile}, record.layer)] = {record.tracks, record.owner};
	}
}

/** Chunk handlers of the metro mod underground. */
static const ChunkHandler metro_underground_chunk_handlers[] = {
	{ 'MTUG', Save_MTUG, Load_MTUG, nullptr, nullptr, ChunkType::Table },
};

extern const ChunkHandlerTable _metro_underground_chunk_handlers(metro_underground_chunk_handlers);
