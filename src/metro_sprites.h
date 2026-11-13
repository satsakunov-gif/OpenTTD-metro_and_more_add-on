/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_sprites.h Metro mod: own sprites from PNG files with vanilla fallbacks, sprites drawn by code (stage 10). */

#ifndef METRO_SPRITES_H
#define METRO_SPRITES_H

#include "gfx_type.h"
#include "direction_type.h"
#include "engine_type.h"
#include "tile_type.h"

#include <span>
#include <string_view>

/** The own sprites; see table/metro_sprite_tables.h and mod-sprites/README.md. */
enum class MetroSpriteId : uint16_t {
	Substation, GasPlant, NuclearPlant, SolarPlant, WindTurbine, VentShaft,
	IconGrid, IconLine, IconLineRemove, IconSubstation, IconGasPlant, IconNuclearPlant, IconSolarPlant, IconWindTurbine, IconVentShaft,
	Entrance, GroundLayer1, GroundLayer2, GroundLayer3,
	TrainFirst, ///< Head and car of each metro series, in the order of the series table.
	TrainLast = TrainFirst + 15,
	TramFirst, ///< The trams in the order of the tram table.
	TramLast = TramFirst + 3,
	School, Hospital, University,
	CarFirst, ///< Cars and vans by years, in the order of the car sprite table.
	CarLast = CarFirst + 5,
	Pedestrian,
	End,
};

SpriteID MetroSprite(MetroSpriteId id, SpriteID fallback, uint index = 0);
SpriteID MetroWalkerSprite(uint index);
bool MetroHasSprite(MetroSpriteId id);
void MetroSpritesLoad();
bool MetroSpritesVehicleImage(EngineID engine, Direction direction, SpriteID &sprite);
SpriteID MetroSpritesObjectImage(TileIndex tile);
bool ConMetroSprites(std::span<std::string_view> argv);

#endif /* METRO_SPRITES_H */
