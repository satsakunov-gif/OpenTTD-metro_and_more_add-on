/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_malls.h Metro mod: underground malls at megahub stations (stage 9). */

#ifndef METRO_MALLS_H
#define METRO_MALLS_H

#include "cargo_type.h"
#include "economy_type.h"
#include "station_type.h"

#include <map>

struct Station;

/** An underground mall of a station. */
struct MetroMall {
	uint8_t size = 1;        ///< 1..3 = S, M, L.
	uint32_t flow = 0;       ///< Passengers of the station this month (boarding and arriving).
	uint32_t last_flow = 0;  ///< Passengers last month.
	Money price = 0;         ///< What it cost (refund and upkeep).
	Money last_income = 0;   ///< Income last month (before upkeep).
	Money last_upkeep = 0;   ///< Upkeep last month.
};

extern std::map<uint16_t, MetroMall> _metro_malls; ///< Station -> mall.

void MetroMallsReset();
void MetroMallsOnStationDeleted(StationID station);
void MetroMallCountPassengers(const Station *st, CargoType cargo, uint amount);
void ShowMetroMallsWindow();
void MetroMallsConsoleList();

#endif /* METRO_MALLS_H */
