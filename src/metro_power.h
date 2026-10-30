/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_power.h Metro mod: power grid — plants, power lines, substations, electric trains, vent shafts and blackouts. */

#ifndef METRO_POWER_H
#define METRO_POWER_H

#include "cargo_type.h"
#include "company_type.h"
#include "economy_type.h"
#include "industry_type.h"
#include "object_type.h"
#include "station_type.h"
#include "strings_type.h"
#include "tile_type.h"

#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct TileInfo;
struct Train;
struct Industry;
struct Station;

/** Buildings of the grid the player can build. Data: table/metro_power_tables.h. */
enum class MetroPowerBuilding : uint8_t {
	Substation,
	GasPlant,
	NuclearPlant,
	SolarPlant,
	WindPlant,
	VentShaft,
	End,
};

static constexpr uint METRO_POWER_BUILDING_COUNT = static_cast<uint>(MetroPowerBuilding::End);

/** What a building is in the grid. */
enum class MetroPowerRole : uint8_t {
	Plant,      ///< Produces power.
	Substation, ///< Feeds tracks and vent shafts around it.
	Vent,       ///< Consumes power (metro ventilation).
};

/** Saved state of a power plant: a grid object or a vanilla power station industry (MTPP chunk). */
struct MetroPowerPlantState {
	uint8_t kind = 0;            ///< 0 = object (#id is an ObjectID), 1 = industry (#id is an IndustryID).
	uint32_t id = 0;             ///< Object or industry index.
	uint32_t fuel = 0;           ///< Fuel in stock.
	uint16_t output = 1000;      ///< Output in 1/1000 of nominal; falls without fuel.
	uint32_t burn_progress = 0;  ///< Fuel burnt but not yet taken from the stock, in 1/(30*1000) units.
	uint32_t used_kw = 0;        ///< Power taken from the plant at the last update (decides the fuel burnt).
};

/** Saved per-company state of the grid (MTPC chunk). */
struct MetroPowerCompanyState {
	uint8_t company = 0;      ///< Company index.
	bool tracks_first = false; ///< Blackout priority swapped: tracks are fed before the ventilation.
};

extern std::map<uint32_t, uint8_t> _metro_power_lines;              ///< Power line tiles and their owners.
extern std::vector<MetroPowerPlantState> _metro_power_plants;       ///< Plants with a state.
extern std::map<uint8_t, MetroPowerCompanyState> _metro_power_companies;

void MetroPowerReset();
void MetroPowerAfterLoad();
void MetroPowerMarkDirty();

/* Buildings. */
bool IsMetroPowerObject(ObjectType type);
MetroPowerBuilding GetMetroPowerBuildingOfObject(ObjectType type);
StringID GetMetroPowerBuildingName(MetroPowerBuilding building);
Money GetMetroPowerBuildingCost(MetroPowerBuilding building);
bool IsMetroPowerBuildingAvailable(MetroPowerBuilding building);
int32_t GetMetroPowerBuildingIntroYear(MetroPowerBuilding building);
uint32_t GetMetroPowerBuildingPowerKw(MetroPowerBuilding building);
Money GetMetroPowerLineCost();
bool MetroPowerClickTile(TileIndex tile);
bool MetroPowerAddAcceptedCargo(TileIndex tile, CargoArray &acceptance, CargoTypes &always_accepted);
bool HasMetroPowerLine(TileIndex tile);
void MetroPowerOnObjectRemoved(uint32_t object);

/* Fuel deliveries. */
void MetroPowerOnIndustryCargo(Industry *i);
void MetroPowerOnCargoDelivered(const Station *st, CargoType cargo, uint amount);
bool IsMetroPowerStationIndustry(const Industry *i);
std::vector<std::string> MetroPowerGetIndustryLines(const Industry *i);

/* Trains. */
uint MetroPowerGetTrainPowerPercent(const Train *front);

/* Drawing. */
void MetroPowerDrawTile(const TileInfo *ti);

/* Network window. */
struct MetroPowerNetworkInfo {
	bool valid = false;
	CompanyID owner = CompanyID::Invalid();
	uint32_t capacity_kw = 0;       ///< Power the plants can give now.
	uint32_t nominal_kw = 0;        ///< Power of the plants at full output.
	uint32_t vent_demand_kw = 0;    ///< Demand of the vent shafts.
	uint32_t track_demand_kw = 0;   ///< Demand of the electric trains.
	uint32_t supplied_kw = 0;       ///< Demand that is fed.
	uint line_tiles = 0;
	uint substations = 0;
	uint substations_off = 0;       ///< Substations cut off by the blackout or without any plant.
	uint vents = 0;
	uint vents_off = 0;             ///< Vent shafts without power and without a diesel.
	uint vents_diesel = 0;          ///< Vent shafts on the emergency diesel.
	uint trains = 0;                ///< Electric trains of the network.
	uint trains_unpowered = 0;      ///< Electric trains without power.
	bool tracks_first = false;
	std::vector<std::string> plants; ///< One line per plant.
};
MetroPowerNetworkInfo MetroPowerGetNetworkInfo(TileIndex tile);
std::vector<std::string> MetroPowerGetBuildingLines(TileIndex tile);
void ShowMetroPowerNetworkWindow(TileIndex tile);
void ShowMetroPowerToolbar();

bool ConMetroPower(std::span<std::string_view> argv);

#endif /* METRO_POWER_H */
