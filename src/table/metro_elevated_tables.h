/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_elevated_tables.h Data tables of the metro mod elevated monorail. */

#ifndef METRO_ELEVATED_TABLES_H
#define METRO_ELEVATED_TABLES_H

#include "sprites.h"

static constexpr uint METRO_ELEVATED_MIN_LEVEL = 2;          ///< Lowest level of an elevated line (the ramp tiles lie below).
static constexpr uint METRO_ELEVATED_MAX_SPAN = 10;          ///< Most tiles under a span: a pillar at least every this many tiles.
static constexpr uint METRO_ELEVATED_HOUSE_CLEARANCE = 2;    ///< Levels between the ground of a house and the span above it.
static constexpr uint32_t METRO_ELEVATED_TALL_HOUSE_POPULATION = 80; ///< Houses from this population are high-rises: no spans over them.
static constexpr uint METRO_ELEVATED_COST_PERCENT_PER_LEVEL = 150; ///< Raising a track piece costs this percent of its price per level.
static constexpr uint METRO_ELEVATED_SPAN_COST_PERCENT = 200;      ///< A span costs this percent of the same ground bridge.

/** Pillar sprites under the elevated track (vanilla steel girder bridge pillars), by axis X and Y. */
static constexpr SpriteID METRO_ELEVATED_PILLAR_X = SPR_BTSGA_X_PILLAR;
static constexpr SpriteID METRO_ELEVATED_PILLAR_Y = SPR_BTSGA_Y_PILLAR;
static constexpr int METRO_ELEVATED_BEAM_THICKNESS = 3;  ///< The vanilla single rail is drawn this many pixels thick as the beam.
static constexpr int METRO_ELEVATED_PILLAR_OFFSET = 7;  ///< The single pillar stands this far into the tile across the beam (middle).

#endif /* METRO_ELEVATED_TABLES_H */
