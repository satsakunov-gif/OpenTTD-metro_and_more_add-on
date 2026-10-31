/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_station_names.h Metro mod: names of new stations after a nearby object or the district. */

#ifndef METRO_STATION_NAMES_H
#define METRO_STATION_NAMES_H

#include "tile_type.h"

#include <span>
#include <string_view>

struct Station;

void MetroNameNewStation(Station *st, TileIndex tile);
bool ConMetroStation(std::span<std::string_view> argv);

#endif /* METRO_STATION_NAMES_H */
