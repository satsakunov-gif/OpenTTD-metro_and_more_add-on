/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file object_sl.cpp Code handling saving and loading of objects */

#include "../stdafx.h"
#include "../object_base.h"
#include "../object_map.h"

#include "saveload.h"
#include "newgrf_sl.h"

#include "../safeguards.h"

static void Save_OBID()
{
	Save_NewGRFMapping(_object_mngr);
}

static void Load_OBID()
{
	Load_NewGRFMapping(_object_mngr);

	/* Metro mod: object types 5..8 are town services since metro_city v2, older NewGRF objects move up. */
	if (!SlXvIsFeaturePresent(XSLFI_METRO_CITY, 2)) {
		auto &mappings = _object_mngr.mappings;
		for (size_t i = mappings.size() - 1; i >= OBJECT_METRO_SCHOOL + METRO_OBJECT_COUNT; i--) mappings[i] = mappings[i - METRO_OBJECT_COUNT];
		for (size_t i = OBJECT_METRO_SCHOOL; i < OBJECT_METRO_SCHOOL + METRO_OBJECT_COUNT; i++) mappings[i] = {};
	}
}

static const ChunkHandler object_chunk_handlers[] = {
	{ 'OBID', Save_OBID, Load_OBID, nullptr,   nullptr, ChunkType::Table },
	MakeUpstreamChunkHandler<'OBJS', GeneralUpstreamChunkLoadInfo>(),
};

extern const ChunkHandlerTable _object_chunk_handlers(object_chunk_handlers);
