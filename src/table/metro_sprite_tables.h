/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_sprite_tables.h Metro mod: own sprites loaded from PNG files (stage 10).
 *
 * Keep in sync with mod-sprites/metro_sprites.csv (the converter reads it): section, name, kind, anchor.
 */

#ifndef METRO_SPRITE_TABLES_H
#define METRO_SPRITE_TABLES_H

#include "../metro_sprites.h"

/** How many images a sprite has and how its files are named. */
enum class MetroSpriteKind : uint8_t {
	Single,     ///< name_<zoom>x.png
	Parts4,     ///< name_<part 0-3>_<zoom>x.png: the tiles of a 2x2 object (part = dy * 2 + dx from the north tile).
	Dir8,       ///< name_<N|NE|E|SE|S|SW|W|NW>_<zoom>x.png: a vehicle in the 8 directions (order of #Direction).
	Diag4x4,    ///< name_<NE|SE|SW|NW>_<frame 0-3>_<zoom>x.png: a walker, 4 directions x 4 frames.
};

/** Where the sprite is attached: decides its offsets. */
enum class MetroSpriteAnchor : uint8_t {
	Building, ///< Bottom centre on the south corner of the tile.
	Building2x2, ///< Bottom centre on the south corner of a 2x2 area drawn from its north tile.
	Ground,   ///< A flat ground tile (64 x 31 at 1x).
	Vehicle,  ///< Centre of the vehicle, a quarter above its bottom.
	Walker,   ///< Feet on the point.
	Icon,     ///< Top left (buttons).
};

struct MetroSpriteSpec {
	const char *section;
	const char *name;
	MetroSpriteKind kind;
	MetroSpriteAnchor anchor;
};

/** All own sprites, in the order of #MetroSpriteId. */
static const MetroSpriteSpec _metro_sprite_specs[] = {
	{ "electricity", "substation",          MetroSpriteKind::Single,  MetroSpriteAnchor::Building },
	{ "electricity", "gas_plant",           MetroSpriteKind::Parts4,  MetroSpriteAnchor::Building },
	{ "electricity", "nuclear_plant",       MetroSpriteKind::Parts4,  MetroSpriteAnchor::Building },
	{ "electricity", "solar_plant",         MetroSpriteKind::Parts4,  MetroSpriteAnchor::Building },
	{ "electricity", "wind_turbine",        MetroSpriteKind::Single,  MetroSpriteAnchor::Building },
	{ "electricity", "vent_shaft",          MetroSpriteKind::Single,  MetroSpriteAnchor::Building },
	{ "electricity", "icon_grid",           MetroSpriteKind::Single,  MetroSpriteAnchor::Icon },
	{ "electricity", "icon_line",           MetroSpriteKind::Single,  MetroSpriteAnchor::Icon },
	{ "electricity", "icon_line_remove",    MetroSpriteKind::Single,  MetroSpriteAnchor::Icon },
	{ "electricity", "icon_substation",     MetroSpriteKind::Single,  MetroSpriteAnchor::Icon },
	{ "electricity", "icon_gas_plant",      MetroSpriteKind::Single,  MetroSpriteAnchor::Icon },
	{ "electricity", "icon_nuclear_plant",  MetroSpriteKind::Single,  MetroSpriteAnchor::Icon },
	{ "electricity", "icon_solar_plant",    MetroSpriteKind::Single,  MetroSpriteAnchor::Icon },
	{ "electricity", "icon_wind_turbine",   MetroSpriteKind::Single,  MetroSpriteAnchor::Icon },
	{ "electricity", "icon_vent_shaft",     MetroSpriteKind::Single,  MetroSpriteAnchor::Icon },
	{ "metro",       "entrance",            MetroSpriteKind::Single,  MetroSpriteAnchor::Building },
	{ "metro",       "ground_layer1",       MetroSpriteKind::Single,  MetroSpriteAnchor::Ground },
	{ "metro",       "ground_layer2",       MetroSpriteKind::Single,  MetroSpriteAnchor::Ground },
	{ "metro",       "ground_layer3",       MetroSpriteKind::Single,  MetroSpriteAnchor::Ground },
	{ "metro",       "train_a_head",        MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "metro",       "train_a_car",         MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "metro",       "train_e_head",        MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "metro",       "train_e_car",         MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "metro",       "train_premetro_head", MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "metro",       "train_premetro_car",  MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "metro",       "train_2000_head",     MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "metro",       "train_2000_car",      MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "metro",       "train_diametr_head",  MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "metro",       "train_diametr_car",   MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "metro",       "train_deep_head",     MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "metro",       "train_deep_car",      MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "metro",       "train_vacuum_head",   MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "metro",       "train_vacuum_car",    MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "metro",       "train_capsule_head",  MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "metro",       "train_capsule_car",   MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "transport",   "tram_t50",            MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "transport",   "tram_t70",            MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "transport",   "tram_lowfloor",       MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "transport",   "tram_aero",           MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "city",        "school",              MetroSpriteKind::Single,  MetroSpriteAnchor::Building },
	{ "city",        "hospital",            MetroSpriteKind::Single,  MetroSpriteAnchor::Building },
	{ "city",        "university",          MetroSpriteKind::Single,  MetroSpriteAnchor::Building },
	{ "city",        "car_1950",            MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "city",        "van_1950",            MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "city",        "car_1965",            MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "city",        "van_1965",            MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "city",        "car_1990",            MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "city",        "van_1990",            MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "city",        "car_1980",            MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "city",        "car_2010",            MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "people",      "pedestrian",          MetroSpriteKind::Diag4x4, MetroSpriteAnchor::Walker },
	{ "metro",       "entrance_big",        MetroSpriteKind::Single,  MetroSpriteAnchor::Building },
	{ "metro",       "entrance_sidewalk",   MetroSpriteKind::Single,  MetroSpriteAnchor::Walker },
	{ "metro",       "wagon_coal",          MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "metro",       "wagon_covered",       MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "metro",       "wagon_mail",          MetroSpriteKind::Dir8,    MetroSpriteAnchor::Vehicle },
	{ "city",        "stadium",             MetroSpriteKind::Single,  MetroSpriteAnchor::Building2x2 },
	{ "city",        "park",                MetroSpriteKind::Single,  MetroSpriteAnchor::Building },
};
static_assert(std::size(_metro_sprite_specs) == to_underlying(MetroSpriteId::End));

/** File name parts of the images of each kind. */
static const char * const _metro_sprite_dir8[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
static const char * const _metro_sprite_diag4[] = { "NE", "SE", "SW", "NW" };

/** Searched for the converted sprites (relative to the search paths of the game, then to the folder of the game). */
static const char * const _metro_sprite_dirs[] = { "mod-sprites/game", "../mod-sprites/game", "metro_sprites" };

static constexpr int METRO_SPRITE_TILE_BOTTOM = 31; ///< The south corner of a flat tile is this far below its north corner at 1x.
static constexpr int METRO_SPRITE_2X2_BOTTOM = 63; ///< The same for a 2x2 area.

#endif /* METRO_SPRITE_TABLES_H */
