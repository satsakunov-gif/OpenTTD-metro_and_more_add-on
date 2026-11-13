/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_walkers.h Metro mod: pedestrians drawn by code (stage 10). */

#ifndef METRO_WALKERS_H
#define METRO_WALKERS_H

#include "gfx_type.h"
#include "tile_type.h"

#include <string>
#include <vector>

/** Which pedestrian sprite to draw: the first of its 16 (4 directions x 4 frames) and the palette of its clothes. */
struct MetroWalkerChoice {
	uint index;     ///< Sprite index of direction NE, frame 0 (add direction * 4 + frame).
	PaletteID pal;  ///< Recolour of the clothes.
	uint style;     ///< #MetroWalkerStyle.
	bool umbrella;
};

MetroWalkerChoice MetroWalkerChoose(TileIndex tile, uint32_t id, uint8_t colour);
bool MetroWalkerDraw(uint index, std::vector<uint8_t> &pixels, uint &width, uint &height);
void MetroWalkersResetPalette();
uint MetroWalkersDump(const std::string &dir);

/* Forced style for tests: -1 = by date and place. */
extern int _metro_walker_force_style;
extern int _metro_walker_force_umbrella;

#endif /* METRO_WALKERS_H */
