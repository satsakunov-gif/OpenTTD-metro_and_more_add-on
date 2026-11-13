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
#include "economy_type.h"
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
static constexpr uint METRO_HEAT_LEVELS = 6;      ///< Colour levels of the map modes.
static constexpr uint METRO_HEAT_FILTER_ROWS = 4; ///< Legend rows before the levels: mode, transport, route, period.

/** Modes of the heat map (the first legend row switches them). */
enum class MetroMapMode : uint8_t {
	PassengerFlow,   ///< Passengers loaded and unloaded at the stations.
	FreightFlow,     ///< Other cargo loaded and unloaded.
	StationLoad,     ///< Cargo waiting at the player's stations.
	StationRating,   ///< Average rating of the player's stations.
	Jams,            ///< Jams on the roads of the towns.
	Profit,          ///< Income of the deliveries at the player's stations.
	Coverage,        ///< How many of the player's stations cover each tile.
	MetroLayers,     ///< Underground tracks by layer, platforms and entrances.
	Ventilation,     ///< Vent shafts and the platforms of -3.
	Grid,            ///< Power lines, buildings and the radius of the substations.
	GridLoad,        ///< Demand of each network against its power.
	Electrification, ///< Tracks with catenary, fed or not.
	ActiveTowns,     ///< Active and other towns.
	TownRating,      ///< Rating of the local authority for the player.
	Districts,       ///< Business and residential districts.
	Tourists,        ///< Tourists delivered last month per town.
	Migration,       ///< People moving in or out of the active towns.
	End,
};

/** Cargo moved at a station by vehicles (loaded + unloaded). */
struct MetroStationTraffic {
	std::array<uint32_t, METRO_HEAT_CATEGORIES> month{};                                          ///< This month.
	std::array<std::array<uint32_t, METRO_HEAT_CATEGORIES>, METRO_HEAT_MONTHS> history{};         ///< [0] = last month.
	uint32_t transfers_month = 0;                                                                 ///< Cargo transferred this month.
	std::array<uint32_t, METRO_HEAT_MONTHS> transfers{};                                          ///< Transfers per month, [0] = last month.
	std::array<Money, METRO_HEAT_CATEGORIES> income_month{};                                      ///< Income of deliveries this month.
	std::array<std::array<Money, METRO_HEAT_CATEGORIES>, METRO_HEAT_MONTHS> income{};             ///< [0] = last month.
};

extern std::map<uint16_t, MetroStationTraffic> _metro_station_traffic;

void MetroHeatReset();
void MetroHeatOnStationDeleted(StationID station);
void MetroHeatRecord(const Station *st, VehicleType type, CargoType cargo, uint amount, uint transfers);
void MetroHeatRecordIncome(const Station *st, VehicleType type, CargoType cargo, Money income);

/* The small map mode. */
void MetroHeatPrepare();
uint8_t MetroHeatLevel(TileIndex tile);
PixelColour MetroHeatLevelColour(uint level);
uint MetroHeatLegendRows();
std::string MetroHeatLegendText(uint row);
PixelColour MetroHeatLegendColour(uint row);
void MetroHeatLegendClick(uint row, bool back);
void MetroHeatSetRoute(VehicleID vehicle);
bool MetroHeatShowsLinks();
void MetroShowHeatSmallMap();
CargoTypes MetroHeatOverlayCargoMask();
std::vector<TileIndex> MetroHeatTransferStations();
TileIndex MetroHeatBusiestStation();
void MetroHeatConsole(const std::vector<std::string_view> &args);

#endif /* METRO_HEATMAP_H */
