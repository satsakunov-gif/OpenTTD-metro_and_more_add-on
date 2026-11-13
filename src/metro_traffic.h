/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_traffic.h Metro mod: private cars, traffic jams and pedestrians in active towns (visual, not saved). */

#ifndef METRO_TRAFFIC_H
#define METRO_TRAFFIC_H

#include "tile_type.h"
#include "direction_type.h"

#include <vector>

#include <span>
#include <string_view>

struct TileInfo;
struct RoadVehicle;

void MetroTrafficReset();
void MetroTrafficDrawTile(const TileInfo *ti);
int MetroTrafficAdjustRoadVehicleSpeed(const RoadVehicle *v, int max_speed);
uint MetroTrafficGetJamPercent(TileIndex tile);

/** A private car or pedestrian, for the cinema camera. */
struct MetroTrafficActor {
	uint32_t id = 0;
	bool pedestrian = false;
	int x = 0;            ///< World pixels.
	int y = 0;
	DiagDirection dir = DiagDirection::Invalid;
	bool jammed = false;  ///< A car stuck in a jam.
	uint16_t town = 0;
};

std::vector<MetroTrafficActor> MetroTrafficListActors();
bool MetroTrafficFindActor(uint32_t id, MetroTrafficActor &actor);

bool ConMetroTraffic(std::span<std::string_view> argv);

#endif /* METRO_TRAFFIC_H */
