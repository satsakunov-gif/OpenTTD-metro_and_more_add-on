/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_underground_sl.cpp Code handling saving and loading of the metro mod underground layers. */

#include "../stdafx.h"
#include "../metro_underground.h"
#include "../metro_overlay.h"
#include "../metro_stations.h"
#include "../metro_malls.h"

#include "saveload.h"

#include "../safeguards.h"

/** One underground tile in the save. */
struct MetroUndergroundRecord {
	uint32_t tile = 0;
	uint8_t layer = 0;
	uint8_t tracks = 0;
	uint8_t owner = 0;
	uint8_t incline = 0;
	uint8_t depot = 0;
};

static const NamedSaveLoad _metro_underground_desc[] = {
	NSL("tile",   SLE_VAR(MetroUndergroundRecord, tile,   SLE_UINT32)),
	NSL("layer",  SLE_VAR(MetroUndergroundRecord, layer,  SLE_UINT8)),
	NSL("tracks", SLE_VAR(MetroUndergroundRecord, tracks, SLE_UINT8)),
	NSL("owner",  SLE_VAR(MetroUndergroundRecord, owner,  SLE_UINT8)),
	NSL("incline", SLE_VAR(MetroUndergroundRecord, incline, SLE_UINT8)),
	NSL("depot",  SLE_VAR(MetroUndergroundRecord, depot,  SLE_UINT8)),
};

static void Save_MTUG()
{
	SaveLoadTableData slt = SlTableHeader(_metro_underground_desc);
	int index = 0;
	for (const auto &[key, ut] : _metro_underground) {
		MetroUndergroundRecord record{static_cast<uint32_t>(key >> 2), static_cast<uint8_t>(key & 3), ut.tracks, ut.owner, ut.incline, ut.depot};
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
		_metro_underground[MetroUndergroundKey(TileIndex{record.tile}, record.layer)] = {record.tracks, record.owner, record.incline, record.depot};
	}
}

/** One full map tile of the underground in the save. */
struct MetroOverlayRecord {
	uint32_t tile = 0;
	uint8_t type = 0;
	uint8_t m1 = 0;
	uint16_t m2 = 0;
	uint8_t m3 = 0;
	uint8_t m4 = 0;
	uint8_t m5 = 0;
	uint8_t m6 = 0;
	uint8_t m7 = 0;
	uint16_t m8 = 0;
};

static const NamedSaveLoad _metro_overlay_desc[] = {
	NSL("tile", SLE_VAR(MetroOverlayRecord, tile, SLE_UINT32)),
	NSL("type", SLE_VAR(MetroOverlayRecord, type, SLE_UINT8)),
	NSL("m1",   SLE_VAR(MetroOverlayRecord, m1,   SLE_UINT8)),
	NSL("m2",   SLE_VAR(MetroOverlayRecord, m2,   SLE_UINT16)),
	NSL("m3",   SLE_VAR(MetroOverlayRecord, m3,   SLE_UINT8)),
	NSL("m4",   SLE_VAR(MetroOverlayRecord, m4,   SLE_UINT8)),
	NSL("m5",   SLE_VAR(MetroOverlayRecord, m5,   SLE_UINT8)),
	NSL("m6",   SLE_VAR(MetroOverlayRecord, m6,   SLE_UINT8)),
	NSL("m7",   SLE_VAR(MetroOverlayRecord, m7,   SLE_UINT8)),
	NSL("m8",   SLE_VAR(MetroOverlayRecord, m8,   SLE_UINT16)),
};

static void Save_MTUO()
{
	SaveLoadTableData slt = SlTableHeader(_metro_overlay_desc);
	int index = 0;
	for (const auto &[tile, o] : _metro_overlay) {
		MetroOverlayRecord r{tile, o.m.type, o.m.m1, o.m.m2, o.m.m3, o.m.m4, o.m.m5, o.me.m6, o.me.m7, o.me.m8};
		SlSetArrayIndex(index++);
		SlObjectSaveFiltered(&r, slt);
	}
}

static void Load_MTUO()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_metro_overlay_desc);
	_metro_overlay.clear();
	while (SlIterateArray() != -1) {
		MetroOverlayRecord r;
		SlObjectLoadFiltered(&r, slt);
		MetroOverlayTile &o = _metro_overlay[r.tile];
		o.m.type = r.type;
		o.m.m1 = r.m1;
		o.m.m2 = r.m2;
		o.m.m3 = r.m3;
		o.m.m4 = r.m4;
		o.m.m5 = r.m5;
		o.me.m6 = r.m6;
		o.me.m7 = r.m7;
		o.me.m8 = r.m8;
	}
}

/** Level of an underground station in the save. */
struct MetroStationLevelRecord {
	uint16_t station = 0;
	uint8_t level = 0;
};

static const NamedSaveLoad _metro_station_level_desc[] = {
	NSL("station", SLE_VAR(MetroStationLevelRecord, station, SLE_UINT16)),
	NSL("level",   SLE_VAR(MetroStationLevelRecord, level,   SLE_UINT8)),
};

static void Save_MTSL()
{
	SaveLoadTableData slt = SlTableHeader(_metro_station_level_desc);
	int index = 0;
	for (const auto &[station, level] : _metro_station_levels) {
		MetroStationLevelRecord r{station, level};
		SlSetArrayIndex(index++);
		SlObjectSaveFiltered(&r, slt);
	}
}

static void Load_MTSL()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_metro_station_level_desc);
	_metro_station_levels.clear();
	while (SlIterateArray() != -1) {
		MetroStationLevelRecord r;
		SlObjectLoadFiltered(&r, slt);
		_metro_station_levels[r.station] = r.level;
	}
}

/** An entrance on a sidewalk or in a basement in the save. */
struct MetroExtraEntranceRecord {
	uint32_t tile = 0;
	uint8_t owner = 0;
	uint8_t kind = 0;
};

static const NamedSaveLoad _metro_extra_entrance_desc[] = {
	NSL("tile",  SLE_VAR(MetroExtraEntranceRecord, tile,  SLE_UINT32)),
	NSL("owner", SLE_VAR(MetroExtraEntranceRecord, owner, SLE_UINT8)),
	NSL("kind",  SLE_VAR(MetroExtraEntranceRecord, kind,  SLE_UINT8)),
};

static void Save_MTEE()
{
	SaveLoadTableData slt = SlTableHeader(_metro_extra_entrance_desc);
	int index = 0;
	for (const auto &[tile, e] : _metro_extra_entrances) {
		MetroExtraEntranceRecord r{tile, e.owner, static_cast<uint8_t>(e.kind)};
		SlSetArrayIndex(index++);
		SlObjectSaveFiltered(&r, slt);
	}
}

static void Load_MTEE()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_metro_extra_entrance_desc);
	_metro_extra_entrances.clear();
	while (SlIterateArray() != -1) {
		MetroExtraEntranceRecord r;
		SlObjectLoadFiltered(&r, slt);
		_metro_extra_entrances[r.tile] = {r.owner, static_cast<MetroEntranceKind>(r.kind)};
	}
}

/** An underground mall in the save. */
struct MetroMallRecord {
	uint16_t station = 0;
	MetroMall mall;
};

static const NamedSaveLoad _metro_mall_desc[] = {
	NSL("station",     SLE_VAR(MetroMallRecord, station,          SLE_UINT16)),
	NSL("size",        SLE_VAR(MetroMallRecord, mall.size,        SLE_UINT8)),
	NSL("flow",        SLE_VAR(MetroMallRecord, mall.flow,        SLE_UINT32)),
	NSL("last_flow",   SLE_VAR(MetroMallRecord, mall.last_flow,   SLE_UINT32)),
	NSL("price",       SLE_VAR(MetroMallRecord, mall.price,       SLE_INT64)),
	NSL("last_income", SLE_VAR(MetroMallRecord, mall.last_income, SLE_INT64)),
	NSL("last_upkeep", SLE_VAR(MetroMallRecord, mall.last_upkeep, SLE_INT64)),
};

static void Save_MTML()
{
	SaveLoadTableData slt = SlTableHeader(_metro_mall_desc);
	int index = 0;
	for (const auto &[station, mall] : _metro_malls) {
		MetroMallRecord r{station, mall};
		SlSetArrayIndex(index++);
		SlObjectSaveFiltered(&r, slt);
	}
}

static void Load_MTML()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_metro_mall_desc);
	_metro_malls.clear();
	while (SlIterateArray() != -1) {
		MetroMallRecord r;
		SlObjectLoadFiltered(&r, slt);
		if (r.mall.size >= 1 && r.mall.size <= 3) _metro_malls[r.station] = r.mall;
	}
}

/** Chunk handlers of the metro mod underground. */
static const ChunkHandler metro_underground_chunk_handlers[] = {
	{ 'MTUG', Save_MTUG, Load_MTUG, nullptr, nullptr, ChunkType::Table },
	{ 'MTUO', Save_MTUO, Load_MTUO, nullptr, nullptr, ChunkType::Table },
	{ 'MTSL', Save_MTSL, Load_MTSL, nullptr, nullptr, ChunkType::Table },
	{ 'MTEE', Save_MTEE, Load_MTEE, nullptr, nullptr, ChunkType::Table },
	{ 'MTML', Save_MTML, Load_MTML, nullptr, nullptr, ChunkType::Table },
};

extern const ChunkHandlerTable _metro_underground_chunk_handlers(metro_underground_chunk_handlers);
