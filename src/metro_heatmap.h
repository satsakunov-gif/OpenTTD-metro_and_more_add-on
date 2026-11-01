/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_heatmap.h Metro mod: heat map mode of the small map — traffic of the stations (stage 9). */

#ifndef METRO_HEATMAP_H
#define METRO_HEATMAP_H

#include "cargo_type.h"
#include "gfx_type.h"
#include "station_type.h"
#include "tile_type.h"
#include "vehicle_type.h"

#include <array>
#include <map>
#include <string>
#include <utility>
#include <vector>

struct Station;

static constexpr uint METRO_HEAT_CATEGORIES = 8;  ///< Vehicle type (train, road, ship, aircraft) x (passengers, other cargo).
static constexpr uint METRO_HEAT_MONTHS = 12;     ///< Months of history.

/** Cargo moved at a station by vehicles (loaded + unloaded). */
struct MetroStationTraffic {
	std::array<uint32_t, METRO_HEAT_CATEGORIES> month{};                                          ///< This month.
	std::array<std::array<uint32_t, METRO_HEAT_CATEGORIES>, METRO_HEAT_MONTHS> history{};         ///< [0] = last month.
	uint32_t transfers_month = 0;                                                                 ///< Cargo transferred this month.
	std::array<uint32_t, METRO_HEAT_MONTHS> transfers{};                                          ///< Transfers per month, [0] = last month.
};

extern std::map<uint16_t, MetroStationTraffic> _metro_station_traffic;

void MetroHeatReset();
void MetroHeatOnStationDeleted(StationID station);
void MetroHeatRecord(const Station *st, VehicleType type, CargoType cargo, uint amount, uint transfers);

/* The small map mode. */
void MetroHeatPrepare();
uint8_t MetroHeatLevel(TileIndex tile);
PixelColour MetroHeatLevelColour(uint level);
uint MetroHeatLegendRows();
std::string MetroHeatLegendText(uint row);
PixelColour MetroHeatLegendColour(uint row);
void MetroHeatLegendClick(uint row);
CargoTypes MetroHeatOverlayCargoMask();
std::vector<TileIndex> MetroHeatTransferStations();
TileIndex MetroHeatBusiestStation();
void MetroHeatConsole(const std::vector<std::string_view> &args);

#endif /* METRO_HEATMAP_H */
