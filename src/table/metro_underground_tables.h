/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_underground_tables.h Data tables of the metro mod underground layers. */

#ifndef METRO_UNDERGROUND_TABLES_H
#define METRO_UNDERGROUND_TABLES_H

#include "strings.h"

static constexpr uint METRO_LAYER_COUNT = 3; ///< Layers -1, -2, -3.

/** A layer of the metro. */
struct MetroLayerSpec {
	StringID name;
	uint16_t cost_percent; ///< Track piece price in percent of a plain rail piece.
};

static const MetroLayerSpec _metro_layer_specs[METRO_LAYER_COUNT] = {
	{ STR_METRO_LAYER_1, 300 },
	{ STR_METRO_LAYER_2, 500 },
	{ STR_METRO_LAYER_3, 800 },
};

/* Opening of the layers (DESIGN.md "Слои"). */
static constexpr int32_t METRO_LAYER_1_YEAR = 1960;            ///< Layer -1 opens.
static constexpr int32_t METRO_LAYER_2_MIN_GAP = 5;            ///< Layer -2 opens at least this many years after -1.
/* Fallback years of the rail types when no engine of the type exists (NewGRF sets without them). */
static constexpr int32_t METRO_FALLBACK_ELECTRIC_YEAR = 1960;
static constexpr int32_t METRO_FALLBACK_MONORAIL_YEAR = 2000;
static constexpr int32_t METRO_FALLBACK_MAGLEV_YEAR = 2025;

/* What may lie above layer -1: roads, small houses (country houses) and open land. */
static constexpr uint32_t METRO_SMALL_HOUSE_POPULATION = 30;   ///< A small house: 1x1 with at most this many residents (as in DESIGN.md "Службы").

/* Entrance: a one-tile pavilion on the surface. */
static constexpr uint METRO_ENTRANCE_COST_FACTOR = 20;         ///< Price in plain rail pieces.

#endif /* METRO_UNDERGROUND_TABLES_H */
