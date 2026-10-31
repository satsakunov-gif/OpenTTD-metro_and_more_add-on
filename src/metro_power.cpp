/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_power.cpp Metro mod: power grid.
 *
 * Chain: fuel -> plant -> power lines -> substations -> consumers. A network is everything of one company that touches:
 * power line tiles, grid buildings (plants, substations, vent shafts) and vanilla coal power stations (industries).
 * Only two kinds of consumers exist: vent shafts of the metro and electric trains (rail types with catenary) of human
 * players. A substation feeds tracks and vent shafts in a square of #METRO_POWER_SUBSTATION_RADIUS tiles around it.
 * When the plants of a network give less than the demand, the ventilation is fed first and then whole substations
 * one by one (the order can be swapped per company); an electric engine of a cut-off substation loses its traction.
 * Towns, mayors and the AI rating do not care about electricity at all.
 */

#include "stdafx.h"
#include "metro_power.h"
#include "metro_power_cmd.h"
#include "cargotype.h"
#include "clear_map.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "console_func.h"
#include "console_type.h"
#include "date_func.h"
#include "economy_func.h"
#include "industry.h"
#include "industry_map.h"
#include "landscape_cmd.h"
#include "news_func.h"
#include "window_gui.h"
#include "object.h"
#include "object_base.h"
#include "newgrf_object.h"
#include "object_map.h"
#include "rail.h"
#include "rail_map.h"
#include "road_map.h"
#include "settings_type.h"
#include "station_base.h"
#include "strings_func.h"
#include "tile_cmd.h"
#include "train.h"
#include "engine_base.h"
#include "engine_func.h"
#include "rail_cmd.h"
#include "vehicle_cmd.h"
#include "network/network_type.h"
#include "tree_map.h"
#include "viewport_func.h"
#include "water_map.h"
#include "window_func.h"
#include "core/backup_type.hpp"
#include "core/string_consumer.hpp"
#include "timer/timer.h"
#include "timer/timer_game_calendar.h"
#include "timer/timer_game_tick.h"
#include "table/metro_power_tables.h"
#include "table/strings.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include "safeguards.h"

std::map<uint32_t, uint8_t> _metro_power_lines;
std::vector<MetroPowerPlantState> _metro_power_plants;
std::map<uint8_t, MetroPowerCompanyState> _metro_power_companies;

/** Index of the vanilla coal power station among the original industry types (IT_POWER_STATION). */
static constexpr IndustryType METRO_VANILLA_POWER_STATION = 1;

/** One network of the grid; rebuilt when the grid changes, not saved. */
struct PowerNetwork {
	CompanyID owner = CompanyID::Invalid();
	uint line_tiles = 0;
	std::vector<uint32_t> plants;      ///< Plant objects.
	std::vector<uint32_t> substations; ///< Substation objects.
	std::vector<uint32_t> vents;       ///< Vent shaft objects.
	std::vector<uint32_t> industries;  ///< Vanilla power stations.

	/* Results of the last update. */
	uint32_t capacity_kw = 0;
	uint32_t nominal_kw = 0;
	uint32_t vent_demand_kw = 0;
	uint32_t track_demand_kw = 0;
	uint32_t supplied_kw = 0;
	std::vector<uint32_t> fed_vents;   ///< Vent shafts covered by the substations of this network.
};

/** Runtime state of a substation. */
struct SubstationState {
	int network = -1;
	uint32_t demand_kw = 0;
	bool powered = false;
	uint trains = 0;
	uint trains_unpowered = 0;
};

/** Runtime state of a vent shaft. */
struct VentState {
	int network = -1;  ///< Network of the substation that feeds it, -1 if none covers it.
	bool grid = false;
	bool diesel = false;
};

static std::vector<PowerNetwork> _networks;
static std::unordered_map<uint32_t, int> _object_network;        ///< Grid object -> network.
static std::unordered_map<uint32_t, uint> _industry_networks;    ///< Power station -> number of networks it feeds.
static std::unordered_map<uint32_t, SubstationState> _substations;
static std::unordered_map<uint32_t, VentState> _vents;
static std::unordered_map<uint32_t, uint8_t> _train_power_percent; ///< Trains with less than full power.
static std::unordered_set<uint32_t> _train_stop_news;            ///< Trains already reported as stopped without power.
static std::map<uint8_t, TileIndex> _company_blackout;          ///< Companies with a blackout going on, and a cut-off substation of it.
static bool _topology_dirty = true;

/* ---------------------------------------------------------------------------------------------- */
/* Buildings                                                                                      */
/* ---------------------------------------------------------------------------------------------- */

static_assert(OBJECT_METRO_GAS_PLANT == OBJECT_METRO_SUBSTATION + to_underlying(MetroPowerBuilding::GasPlant));
static_assert(OBJECT_METRO_VENT_SHAFT == OBJECT_METRO_SUBSTATION + to_underlying(MetroPowerBuilding::VentShaft));
static_assert(METRO_POWER_BUILDING_COUNT == METRO_POWER_OBJECT_COUNT);

static const MetroPowerBuildingSpec &GetBuildingSpec(MetroPowerBuilding building)
{
	return _metro_power_building_specs[to_underlying(building)];
}

bool IsMetroPowerObject(ObjectType type)
{
	return type >= OBJECT_METRO_SUBSTATION && type < OBJECT_METRO_SUBSTATION + METRO_POWER_OBJECT_COUNT;
}

MetroPowerBuilding GetMetroPowerBuildingOfObject(ObjectType type)
{
	return static_cast<MetroPowerBuilding>(type - OBJECT_METRO_SUBSTATION);
}

StringID GetMetroPowerBuildingName(MetroPowerBuilding building)
{
	return GetBuildingSpec(building).name;
}

Money GetMetroPowerBuildingCost(MetroPowerBuilding building)
{
	return _price[Price::TownAction] * GetBuildingSpec(building).cost_factor >> 8;
}

int32_t GetMetroPowerBuildingIntroYear(MetroPowerBuilding building)
{
	return GetBuildingSpec(building).intro_year;
}

bool IsMetroPowerBuildingAvailable(MetroPowerBuilding building)
{
	return CalTime::CurYear().base() >= GetBuildingSpec(building).intro_year;
}

uint32_t GetMetroPowerBuildingPowerKw(MetroPowerBuilding building)
{
	return GetBuildingSpec(building).power_kw;
}

Money GetMetroPowerLineCost()
{
	return _price[Price::BuildRail] * METRO_POWER_LINE_COST_FACTOR;
}

/** Grid object with the index, or nullptr. */
static const Object *GetPowerObject(uint32_t index)
{
	const Object *o = Object::GetIfValid(index);
	return (o != nullptr && IsMetroPowerObject(o->type)) ? o : nullptr;
}

/** Grid object on the tile, or nullptr. */
static const Object *GetPowerObjectAt(TileIndex tile)
{
	if (!IsTileType(tile, TileType::Object)) return nullptr;
	const Object *o = Object::GetByTile(tile);
	return IsMetroPowerObject(o->type) ? o : nullptr;
}

static MetroPowerBuilding GetBuilding(const Object *o)
{
	return GetMetroPowerBuildingOfObject(o->type);
}

bool IsMetroPowerStationIndustry(const Industry *i)
{
	const IndustrySpec *spec = GetIndustrySpec(i->type);
	IndustryType original = i->type < NEW_INDUSTRYOFFSET ? i->type : spec->grf_prop.subst_id;
	if (original != METRO_VANILLA_POWER_STATION) return false;
	CargoType coal = GetCargoTypeByLabel(CT_COAL);
	return IsValidCargoType(coal) && i->GetCargoAcceptedIndex(coal) >= 0;
}

/* ---------------------------------------------------------------------------------------------- */
/* Plant states                                                                                   */
/* ---------------------------------------------------------------------------------------------- */

static MetroPowerPlantState *FindPlantState(uint8_t kind, uint32_t id)
{
	for (MetroPowerPlantState &state : _metro_power_plants) {
		if (state.kind == kind && state.id == id) return &state;
	}
	return nullptr;
}

/** State of a plant, created when missing. Plants burning fuel start without output. */
static MetroPowerPlantState &GetPlantState(uint8_t kind, uint32_t id, bool needs_fuel)
{
	if (MetroPowerPlantState *state = FindPlantState(kind, id); state != nullptr) return *state;
	MetroPowerPlantState &state = _metro_power_plants.emplace_back();
	state.kind = kind;
	state.id = id;
	state.output = needs_fuel ? 0 : 1000;
	return state;
}

static MetroPowerPlantState &GetObjectPlantState(const Object *o)
{
	return GetPlantState(0, o->index.base(), GetBuildingSpec(GetBuilding(o)).fuel != CT_INVALID);
}

static MetroPowerPlantState &GetIndustryPlantState(const Industry *i)
{
	return GetPlantState(1, i->index.base(), true);
}

/** Nominal output of a plant state. */
static uint32_t GetPlantNominalKw(const MetroPowerPlantState &state)
{
	if (state.kind == 1) return METRO_POWER_COAL_STATION_KW;
	const Object *o = GetPowerObject(state.id);
	return o == nullptr ? 0 : GetBuildingSpec(GetBuilding(o)).power_kw;
}

static uint16_t GetPlantFuelPerMonth(const MetroPowerPlantState &state)
{
	if (state.kind == 1) return METRO_POWER_COAL_PER_MONTH;
	const Object *o = GetPowerObject(state.id);
	return o == nullptr ? 0 : GetBuildingSpec(GetBuilding(o)).fuel_per_month;
}

static uint32_t GetPlantCapacityKw(const MetroPowerPlantState &state)
{
	return static_cast<uint32_t>(static_cast<uint64_t>(GetPlantNominalKw(state)) * state.output / 1000);
}

void MetroPowerOnIndustryCargo(Industry *i)
{
	if (!IsMetroPowerStationIndustry(i)) return;
	const IndustrySpec *spec = GetIndustrySpec(i->type);
	/* Industries with production callbacks keep the cargo themselves; only the plain vanilla station is read. */
	if (spec->callback_mask != IndustryCallbackMasks{}) return;
	CargoType coal = GetCargoTypeByLabel(CT_COAL);
	for (const auto &acc : i->Accepted()) {
		if (acc.cargo != coal || acc.waiting == 0) continue;
		MetroPowerPlantState &state = GetIndustryPlantState(i);
		state.fuel = std::min<uint32_t>(state.fuel + acc.waiting, METRO_POWER_FUEL_STOCK_MAX);
		SetWindowDirty(WindowClass::IndustryView, i->index);
	}
}

void MetroPowerOnCargoDelivered(const Station *st, CargoType cargo, uint amount)
{
	if (amount == 0) return;
	std::vector<const Object *> plants;
	for (const Object *o : Object::Iterate()) {
		if (!IsMetroPowerObject(o->type) || GetTileOwner(o->location.tile) != st->owner) continue;
		CargoLabel fuel = GetBuildingSpec(GetBuilding(o)).fuel;
		if (fuel == CT_INVALID || GetCargoTypeByLabel(fuel) != cargo) continue;
		for (TileIndex tile : o->location) {
			if (st->TileIsInCatchment(tile)) {
				plants.push_back(o);
				break;
			}
		}
	}
	if (plants.empty()) return;
	uint share = amount / static_cast<uint>(plants.size());
	uint rest = amount % static_cast<uint>(plants.size());
	for (const Object *o : plants) {
		MetroPowerPlantState &state = GetObjectPlantState(o);
		state.fuel = std::min<uint32_t>(state.fuel + share + (rest > 0 ? 1 : 0), METRO_POWER_FUEL_STOCK_MAX);
		if (rest > 0) rest--;
	}
	InvalidateWindowClassesData(WindowClass::MetroPowerNetwork);
}

bool MetroPowerAddAcceptedCargo(TileIndex tile, CargoArray &acceptance, CargoTypes &always_accepted)
{
	const Object *o = GetPowerObjectAt(tile);
	if (o == nullptr) return false;
	CargoLabel fuel = GetBuildingSpec(GetBuilding(o)).fuel;
	if (fuel == CT_INVALID) return true;
	CargoType cargo = GetCargoTypeByLabel(fuel);
	if (IsValidCargoType(cargo)) {
		acceptance[cargo] += 8;
		always_accepted.Set(cargo);
	}
	return true;
}

/** Daily: burn fuel by the load of the last update; without fuel the output fades within a month. */
static void UpdatePlantsDaily()
{
	/* Drop states of plants that are gone. */
	std::erase_if(_metro_power_plants, [](const MetroPowerPlantState &state) {
		if (state.kind == 1) {
			const Industry *i = Industry::GetIfValid(state.id);
			return i == nullptr || !IsMetroPowerStationIndustry(i);
		}
		const Object *o = GetPowerObject(state.id);
		return o == nullptr || GetBuildingSpec(GetBuilding(o)).role != MetroPowerRole::Plant;
	});

	for (MetroPowerPlantState &state : _metro_power_plants) {
		uint16_t per_month = GetPlantFuelPerMonth(state);
		if (per_month == 0) {
			state.output = 1000;
			continue;
		}
		uint32_t nominal = std::max<uint32_t>(GetPlantNominalKw(state), 1);
		state.burn_progress += static_cast<uint32_t>(static_cast<uint64_t>(per_month) * 1000 * std::min(state.used_kw, nominal) / nominal);
		while (state.burn_progress >= 30 * 1000 && state.fuel > 0) {
			state.fuel--;
			state.burn_progress -= 30 * 1000;
		}
		if (state.fuel > 0) {
			state.output = 1000;
		} else {
			state.burn_progress = 0;
			uint16_t fade = 1000 / METRO_POWER_NO_FUEL_FADE_DAYS;
			state.output = state.output > fade ? state.output - fade : 0;
		}
	}
}

/* ---------------------------------------------------------------------------------------------- */
/* Power lines                                                                                    */
/* ---------------------------------------------------------------------------------------------- */

bool HasMetroPowerLine(TileIndex tile)
{
	return !_metro_power_lines.empty() && _metro_power_lines.contains(tile.base());
}

/** Whether a power line can stand on the tile: open land, roads, rails and water, not buildings. */
static CommandCost CheckPowerLineTile(TileIndex tile)
{
	switch (GetTileType(tile)) {
		case TileType::Clear:
		case TileType::Trees:
			return CommandCost();

		case TileType::Railway:
			if (IsRailDepot(tile)) return CommandCost(STR_METRO_POWER_ERR_BUILDING);
			return CommandCost();

		case TileType::Road:
			if (IsRoadDepot(tile)) return CommandCost(STR_METRO_POWER_ERR_BUILDING);
			return CommandCost();

		case TileType::Water:
			if (IsShipDepot(tile) || IsLock(tile)) return CommandCost(STR_METRO_POWER_ERR_BUILDING);
			return CommandCost();

		default:
			return CommandCost(STR_METRO_POWER_ERR_BUILDING);
	}
}

/** Remove power lines whose tile got a building or whose owner is gone. */
static void ValidatePowerLines()
{
	std::erase_if(_metro_power_lines, [](const auto &line) {
		TileIndex tile{line.first};
		bool ok = tile < Map::Size() && Company::IsValidID(CompanyID{line.second}) && CheckPowerLineTile(tile).Succeeded();
		if (!ok) {
			_topology_dirty = true;
			if (tile < Map::Size()) MarkTileDirtyByTile(tile);
		}
		return !ok;
	});
}

/**
 * Build or remove a straight power line.
 * @param flags Type of operation.
 * @param tile End tile.
 * @param start Start tile.
 * @param remove Remove the line instead of building it.
 * @return The cost of this operation or an error.
 */
CommandCost CmdMetroBuildPowerLine(DoCommandFlags flags, TileIndex tile, TileIndex start, bool remove)
{
	if (!Company::IsValidID(_current_company) || start >= Map::Size()) return CMD_ERROR;
	if (TileX(start) != TileX(tile) && TileY(start) != TileY(tile)) return CommandCost(STR_METRO_POWER_ERR_STRAIGHT);

	CommandCost cost(ExpensesType::Construction);
	CommandCost last_error(remove ? STR_METRO_POWER_ERR_NO_LINE : STR_ERROR_ALREADY_BUILT);
	bool done = false;
	for (TileIndex t : TileArea(start, tile)) {
		auto it = _metro_power_lines.find(t.base());
		if (remove) {
			if (it == _metro_power_lines.end()) continue;
			if (CompanyID{it->second} != _current_company) {
				last_error = CommandCost(STR_METRO_POWER_ERR_NOT_OWNER);
				continue;
			}
			cost.AddCost(_price[Price::BuildRail] * METRO_POWER_LINE_REMOVE_FACTOR);
			done = true;
			if (flags.Test(DoCommandFlag::Execute)) {
				_metro_power_lines.erase(it);
				MarkTileDirtyByTile(t);
			}
			continue;
		}

		if (it != _metro_power_lines.end()) continue;
		CommandCost ret = CheckPowerLineTile(t);
		if (ret.Failed()) {
			last_error = ret;
			continue;
		}
		cost.AddCost(GetMetroPowerLineCost());
		done = true;
		if (flags.Test(DoCommandFlag::Execute)) {
			_metro_power_lines[t.base()] = _current_company.base();
			MarkTileDirtyByTile(t);
		}
	}
	if (!done) return last_error;
	if (flags.Test(DoCommandFlag::Execute)) MetroPowerMarkDirty();
	return cost;
}

/* ---------------------------------------------------------------------------------------------- */
/* Building the grid buildings                                                                    */
/* ---------------------------------------------------------------------------------------------- */

extern CommandCost CheckBuildableTile(TileIndex tile, DiagDirections invalid_dirs, int &allowed_z, bool allow_steep, bool check_bridge);

/**
 * Build a plant, substation or vent shaft.
 * @param flags Type of operation.
 * @param tile North tile.
 * @param building #MetroPowerBuilding to build.
 * @return The cost of this operation or an error.
 */
CommandCost CmdMetroBuildPowerObject(DoCommandFlags flags, TileIndex tile, uint8_t building)
{
	if (building >= METRO_POWER_BUILDING_COUNT || !Company::IsValidID(_current_company)) return CMD_ERROR;
	MetroPowerBuilding type = static_cast<MetroPowerBuilding>(building);
	const MetroPowerBuildingSpec &spec = GetBuildingSpec(type);
	if (!IsMetroPowerBuildingAvailable(type)) return CommandCostWithParam(STR_METRO_POWER_ERR_YEAR, spec.intro_year);
	if (!Object::CanAllocateItem()) return CommandCost(STR_ERROR_TOO_MANY_OBJECTS);

	const ObjectSpec *ospec = ObjectSpec::Get(spec.object);
	TileArea area(tile, GB(ospec->size, 0, 4), GB(ospec->size, 4, 4));
	if (TileX(tile) + area.w >= Map::MaxX() || TileY(tile) + area.h >= Map::MaxY()) return CommandCost(STR_ERROR_TOO_CLOSE_TO_EDGE_OF_MAP);

	CommandCost cost(ExpensesType::Construction, GetMetroPowerBuildingCost(type));
	int allowed_z = -1;
	for (TileIndex t : area) {
		if (IsBridgeAbove(t)) return CommandCost(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);
		if (IsWaterTile(t)) return CommandCost(STR_ERROR_CAN_T_BUILD_ON_WATER);
		if (HasMetroPowerLine(t)) return CommandCost(STR_METRO_POWER_ERR_LINE_IN_WAY);
		CommandCost ret = Command<Commands::LandscapeClear>::Do(DoCommandFlags{flags}.Reset(DoCommandFlag::Execute), t);
		if (ret.Failed()) return ret;
		cost.AddCost(std::move(ret));
		ret = CheckBuildableTile(t, {}, allowed_z, false, false);
		if (ret.Failed()) return ret;
		cost.AddCost(std::move(ret));
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		for (TileIndex t : area) Command<Commands::LandscapeClear>::Do(flags, t);
		BuildObject(spec.object, tile, _current_company, nullptr);
		if (spec.role == MetroPowerRole::Plant) GetObjectPlantState(Object::GetByTile(tile));
		MetroPowerMarkDirty();
	}
	return cost;
}

/** Set the blackout priority of the company. */
CommandCost CmdMetroSetPowerPriority(DoCommandFlags flags, bool tracks_first)
{
	if (!Company::IsValidID(_current_company)) return CMD_ERROR;
	if (flags.Test(DoCommandFlag::Execute)) {
		MetroPowerCompanyState &state = _metro_power_companies[_current_company.base()];
		state.company = _current_company.base();
		state.tracks_first = tracks_first;
		MetroPowerMarkDirty();
		InvalidateWindowClassesData(WindowClass::MetroPowerNetwork);
	}
	return CommandCost();
}

static bool IsTracksFirst(CompanyID company)
{
	auto it = _metro_power_companies.find(company.base());
	return it != _metro_power_companies.end() && it->second.tracks_first;
}

void MetroPowerOnObjectRemoved(uint32_t object)
{
	std::erase_if(_metro_power_plants, [object](const MetroPowerPlantState &state) { return state.kind == 0 && state.id == object; });
	MetroPowerMarkDirty();
}

/* ---------------------------------------------------------------------------------------------- */
/* Networks                                                                                       */
/* ---------------------------------------------------------------------------------------------- */

void MetroPowerMarkDirty()
{
	_topology_dirty = true;
}

/** Find the networks: flood fill over the lines, grid buildings and power stations touching each other. */
static void RebuildNetworks()
{
	_topology_dirty = false;
	_networks.clear();
	_object_network.clear();
	_industry_networks.clear();

	/* Tiles of the vanilla power stations, shared by all companies. */
	std::unordered_map<uint32_t, uint32_t> station_tiles; // tile -> industry
	for (const Industry *i : Industry::Iterate()) {
		if (!IsMetroPowerStationIndustry(i)) continue;
		for (TileIndex t : i->location) {
			if (IsTileType(t, TileType::Industry) && GetIndustryIndex(t) == i->index) station_tiles[t.base()] = i->index.base();
		}
	}

	for (const Company *c : Company::Iterate()) {
		std::unordered_map<uint32_t, uint32_t> object_tiles; // tile -> object
		for (const Object *o : Object::Iterate()) {
			if (!IsMetroPowerObject(o->type) || GetTileOwner(o->location.tile) != c->index) continue;
			for (TileIndex t : o->location) object_tiles[t.base()] = o->index.base();
		}
		std::vector<uint32_t> starts;
		for (const auto &[tile, owner] : _metro_power_lines) {
			if (owner == c->index.base()) starts.push_back(tile);
		}
		for (const auto &[tile, object] : object_tiles) starts.push_back(tile);
		std::sort(starts.begin(), starts.end());

		auto is_node = [&](uint32_t t) {
			if (object_tiles.contains(t) || station_tiles.contains(t)) return true;
			auto it = _metro_power_lines.find(t);
			return it != _metro_power_lines.end() && it->second == c->index.base();
		};

		std::unordered_set<uint32_t> visited;
		for (uint32_t start : starts) {
			if (visited.contains(start)) continue;
			PowerNetwork net;
			net.owner = c->index;
			std::unordered_set<uint32_t> objects;
			std::unordered_set<uint32_t> industries;
			std::vector<uint32_t> stack{start};
			visited.insert(start);
			while (!stack.empty()) {
				uint32_t t = stack.back();
				stack.pop_back();
				if (auto it = object_tiles.find(t); it != object_tiles.end()) {
					objects.insert(it->second);
				} else if (auto st = station_tiles.find(t); st != station_tiles.end()) {
					industries.insert(st->second);
				} else {
					net.line_tiles++;
				}
				TileIndex tile{t};
				for (DiagDirection dir : EnumRange(DiagDirection::End)) {
					TileIndex next = TileAddByDiagDir(tile, dir);
					if (!IsValidTile(next) || visited.contains(next.base()) || !is_node(next.base())) continue;
					visited.insert(next.base());
					stack.push_back(next.base());
				}
			}
			if (objects.empty() && industries.empty()) continue;

			int index = static_cast<int>(_networks.size());
			for (uint32_t object : objects) {
				_object_network[object] = index;
				switch (GetBuildingSpec(GetBuilding(Object::Get(object))).role) {
					case MetroPowerRole::Plant: net.plants.push_back(object); break;
					case MetroPowerRole::Substation: net.substations.push_back(object); break;
					case MetroPowerRole::Vent: net.vents.push_back(object); break;
				}
			}
			net.industries.assign(industries.begin(), industries.end());
			std::sort(net.plants.begin(), net.plants.end());
			std::sort(net.substations.begin(), net.substations.end());
			std::sort(net.vents.begin(), net.vents.end());
			std::sort(net.industries.begin(), net.industries.end());
			for (uint32_t ind : net.industries) _industry_networks[ind]++;
			_networks.push_back(std::move(net));
		}
	}
	InvalidateWindowClassesData(WindowClass::MetroPowerNetwork);
}

/** Whether the engine part takes its power from the catenary: all its rail types have wires. */
static bool NeedsGrid(const Train *u)
{
	if (u->railtypes.None()) return false;
	for (RailType rt : u->railtypes) {
		if (!GetRailTypeInfo(rt)->flags.Test(RailTypeFlag::Catenary)) return false;
	}
	return true;
}

/** Power of a part of a train in hp, as Train::GetPower and GetPoweredPartPower without the track check. */
static uint32_t GetPartPower(const Train *u)
{
	uint32_t hp = 0;
	if (!u->IsArticulatedPart()) {
		hp = RailVehInfo(u->engine_type)->power;
		if (u->IsMultiheaded()) hp /= 2;
	}
	if (u->flags.Test(VehicleRailFlag::PoweredWagon)) hp += RailVehInfo(u->gcache.first_engine)->pow_wag_power;
	return hp;
}

/** Substations of each company with their tile, for the coverage search. */
using SubstationList = std::vector<std::pair<uint32_t, TileIndex>>;

/** Nearest substation of the list covering the tile, or UINT32_MAX. */
static uint32_t FindCoveringSubstation(const SubstationList &list, TileIndex tile)
{
	uint32_t best = UINT32_MAX;
	uint best_dist = UINT_MAX;
	for (const auto &[object, sub_tile] : list) {
		uint dx = Delta(TileX(sub_tile), TileX(tile));
		uint dy = Delta(TileY(sub_tile), TileY(tile));
		if (dx > METRO_POWER_SUBSTATION_RADIUS || dy > METRO_POWER_SUBSTATION_RADIUS) continue;
		if (dx + dy < best_dist) {
			best_dist = dx + dy;
			best = object;
		}
	}
	return best;
}

/** Whether the electric trains of the company need the grid. */
static bool CompanyTrainsNeedGrid(CompanyID company)
{
	return _settings_game.vehicle.metro_trains_need_grid && Company::IsValidHumanID(company);
}

/** One powered part of a train, for the second pass. */
struct TrainPart {
	uint32_t substation; ///< Substation feeding the part, UINT32_MAX for none.
	uint32_t hp;
};

/** Update supply, demand, blackouts and the power of the electric trains. */
static void UpdatePower()
{
	if (_topology_dirty) RebuildNetworks();

	/* Substations of each company. */
	std::map<uint8_t, SubstationList> company_substations;
	_substations.clear();
	for (size_t n = 0; n < _networks.size(); n++) {
		for (uint32_t object : _networks[n].substations) {
			const Object *o = Object::Get(object);
			company_substations[_networks[n].owner.base()].emplace_back(object, o->location.tile);
			_substations[object].network = static_cast<int>(n);
		}
	}
	/* Substations without any network (no line, no plant) still cover tiles, but have no power. */
	for (const Object *o : Object::Iterate()) {
		if (!IsMetroPowerObject(o->type) || GetBuildingSpec(GetBuilding(o)).role != MetroPowerRole::Substation) continue;
		if (_substations.contains(o->index.base())) continue;
		company_substations[GetTileOwner(o->location.tile).base()].emplace_back(o->index.base(), o->location.tile);
		_substations[o->index.base()].network = -1;
	}
	for (auto &[company, list] : company_substations) std::sort(list.begin(), list.end());

	/* Capacity of each network. */
	for (PowerNetwork &net : _networks) {
		net.capacity_kw = 0;
		net.nominal_kw = 0;
		net.vent_demand_kw = 0;
		net.track_demand_kw = 0;
		net.supplied_kw = 0;
		net.fed_vents.clear();
		for (uint32_t object : net.plants) {
			const MetroPowerPlantState &state = GetObjectPlantState(Object::Get(object));
			net.capacity_kw += GetPlantCapacityKw(state);
			net.nominal_kw += GetPlantNominalKw(state);
		}
		for (uint32_t ind : net.industries) {
			const MetroPowerPlantState &state = GetIndustryPlantState(Industry::Get(ind));
			uint share = std::max<uint>(_industry_networks[ind], 1);
			net.capacity_kw += GetPlantCapacityKw(state) / share;
			net.nominal_kw += GetPlantNominalKw(state) / share;
		}
	}

	/* Vent shafts take power from the nearest covering substation. */
	_vents.clear();
	for (const Object *o : Object::Iterate()) {
		if (!IsMetroPowerObject(o->type) || GetBuildingSpec(GetBuilding(o)).role != MetroPowerRole::Vent) continue;
		VentState &vent = _vents[o->index.base()];
		auto it = company_substations.find(GetTileOwner(o->location.tile).base());
		if (it == company_substations.end()) continue;
		uint32_t sub = FindCoveringSubstation(it->second, o->location.tile);
		if (sub == UINT32_MAX) continue;
		vent.network = _substations[sub].network;
		if (vent.network >= 0) _networks[vent.network].fed_vents.push_back(o->index.base());
	}

	/* Demand of the electric trains. */
	std::vector<std::pair<const Train *, std::vector<TrainPart>>> trains;
	std::vector<std::pair<const Train *, uint32_t>> free_power; // power of parts that do not need the grid
	for (const Train *front : Train::Iterate()) {
		if (!front->IsFrontEngine() || !CompanyTrainsNeedGrid(front->owner) || front->vehstatus.Test(VehState::Crashed)) continue;
		auto it = company_substations.find(front->owner.base());
		bool idle = front->vehstatus.Test(VehState::Stopped) || front->IsChainInDepot() || front->current_order.IsType(OT_LOADING) ||
				front->flags.Test(VehicleRailFlag::BreakdownStopped);
		std::vector<TrainPart> parts;
		uint32_t free = 0;
		for (const Train *u = front; u != nullptr; u = u->Next()) {
			uint32_t hp = GetPartPower(u);
			if (hp == 0) continue;
			if (!NeedsGrid(u)) {
				free += hp;
				continue;
			}
			uint32_t sub = it == company_substations.end() ? UINT32_MAX : FindCoveringSubstation(it->second, u->tile);
			parts.push_back({sub, hp});
			if (sub != UINT32_MAX) {
				SubstationState &state = _substations[sub];
				if (!idle) state.demand_kw += hp * METRO_POWER_HP_TO_KW_PERMILLE / 1000;
			}
		}
		if (parts.empty()) continue;
		trains.emplace_back(front, std::move(parts));
		free_power.emplace_back(front, free);
	}

	/* Feed the consumers of each network by priority. */
	uint32_t year = CalTime::CurYear().base();
	for (size_t n = 0; n < _networks.size(); n++) {
		PowerNetwork &net = _networks[n];
		uint32_t avail = net.capacity_kw;
		auto feed_vents = [&]() {
			for (uint32_t object : net.fed_vents) {
				uint32_t kw = GetBuildingSpec(MetroPowerBuilding::VentShaft).power_kw;
				net.vent_demand_kw += kw;
				VentState &vent = _vents[object];
				vent.grid = avail >= kw && net.capacity_kw > 0;
				if (vent.grid) avail -= kw;
			}
		};
		auto feed_tracks = [&]() {
			for (uint32_t object : net.substations) {
				SubstationState &state = _substations[object];
				net.track_demand_kw += state.demand_kw;
				state.powered = net.capacity_kw > 0 && state.demand_kw <= avail;
				if (state.powered) avail -= state.demand_kw;
			}
		};
		if (IsTracksFirst(net.owner)) {
			feed_tracks();
			feed_vents();
		} else {
			feed_vents();
			feed_tracks();
		}
		net.supplied_kw = net.capacity_kw - avail;

		/* The load of the plants decides the fuel they burn. */
		auto set_used = [&](MetroPowerPlantState &state, uint32_t capacity) {
			state.used_kw = net.capacity_kw == 0 ? 0 : static_cast<uint32_t>(static_cast<uint64_t>(net.supplied_kw) * capacity / net.capacity_kw);
		};
		for (uint32_t object : net.plants) {
			MetroPowerPlantState &state = GetObjectPlantState(Object::Get(object));
			set_used(state, GetPlantCapacityKw(state));
		}
		for (uint32_t ind : net.industries) {
			MetroPowerPlantState &state = GetIndustryPlantState(Industry::Get(ind));
			set_used(state, GetPlantCapacityKw(state) / std::max<uint>(_industry_networks[ind], 1));
		}
	}
	for (auto &[object, vent] : _vents) vent.diesel = !vent.grid && year >= static_cast<uint32_t>(METRO_POWER_DIESEL_YEAR);

	/* Power of the trains. */
	std::unordered_map<uint32_t, uint8_t> old_percent;
	old_percent.swap(_train_power_percent);
	for (size_t i = 0; i < trains.size(); i++) {
		const Train *front = trains[i].first;
		uint32_t total = free_power[i].second;
		uint32_t powered = free_power[i].second;
		bool any_off = false;
		std::vector<uint32_t> counted;
		for (const TrainPart &part : trains[i].second) {
			total += part.hp;
			bool on = part.substation != UINT32_MAX && _substations[part.substation].powered;
			if (on) {
				powered += part.hp;
			} else {
				any_off = true;
			}
			if (part.substation != UINT32_MAX && std::ranges::find(counted, part.substation) == counted.end()) {
				counted.push_back(part.substation);
				SubstationState &state = _substations[part.substation];
				state.trains++;
				if (!on) state.trains_unpowered++;
			}
		}
		if (!any_off || total == 0) {
			_train_stop_news.erase(front->index.base());
			continue;
		}
		uint8_t percent = static_cast<uint8_t>(powered * 100 / total);
		_train_power_percent[front->index.base()] = percent;
		if (old_percent.find(front->index.base()) == old_percent.end() || old_percent[front->index.base()] != percent) {
			SetWindowDirty(WindowClass::VehicleView, front->index);
		}

		/* News when a train without any power has come to a stop. */
		if (percent == 0 && front->cur_speed == 0 && front->owner == _local_company && !_train_stop_news.contains(front->index.base()) &&
				!front->current_order.IsType(OT_LOADING) && !front->vehstatus.Test(VehState::Stopped) && !front->IsChainInDepot()) {
			_train_stop_news.insert(front->index.base());
			AddVehicleAdviceNewsItem(AdviceType::TrainStuck, GetEncodedString(STR_METRO_POWER_NEWS_TRAIN_STOPPED, front->index), front->index);
		}
	}
	for (const auto &[train, percent] : old_percent) {
		if (!_train_power_percent.contains(train)) SetWindowDirty(WindowClass::VehicleView, VehicleID{train});
	}

	/* Blackout news: a substation with demand is cut off although the network has plants. */
	std::map<uint8_t, TileIndex> blackout;
	for (const auto &[object, state] : _substations) {
		if (state.network < 0 || state.powered || state.demand_kw == 0 || _networks[state.network].capacity_kw == 0) continue;
		uint8_t owner = _networks[state.network].owner.base();
		TileIndex tile = Object::Get(object)->location.tile;
		if (!blackout.contains(owner) || tile < blackout[owner]) blackout[owner] = tile;
	}
	for (const auto &[company, tile] : blackout) {
		if (!_company_blackout.contains(company) && CompanyID{company} == _local_company) {
			AddTileNewsItem(GetEncodedString(STR_METRO_POWER_NEWS_BLACKOUT), NewsType::Advice, tile);
		}
	}
	for (const auto &[company, tile] : _company_blackout) {
		if (!blackout.contains(company) && CompanyID{company} == _local_company && IsValidTile(tile)) {
			AddTileNewsItem(GetEncodedString(STR_METRO_POWER_NEWS_BLACKOUT_OVER), NewsType::Advice, tile);
		}
	}
	_company_blackout.swap(blackout);

	SetWindowClassesDirty(WindowClass::MetroPowerNetwork);
}

uint MetroPowerGetTrainPowerPercent(const Train *front)
{
	if (_train_power_percent.empty()) return 100;
	auto it = _train_power_percent.find(front->index.base());
	return it == _train_power_percent.end() ? 100 : it->second;
}

/** Monthly upkeep of the lines and buildings. */
static void PayUpkeep()
{
	std::map<uint8_t, Money> costs;
	for (const auto &[tile, owner] : _metro_power_lines) costs[owner] += _price[Price::BuildRail] >> METRO_POWER_LINE_UPKEEP_SHIFT;
	for (const Object *o : Object::Iterate()) {
		if (!IsMetroPowerObject(o->type)) continue;
		MetroPowerBuilding building = GetBuilding(o);
		costs[GetTileOwner(o->location.tile).base()] += GetMetroPowerBuildingCost(building) * GetBuildingSpec(building).upkeep_percent / 100 / 12;
	}
	for (const auto &[company, cost] : costs) {
		if (!Company::IsValidID(CompanyID{company}) || cost == 0) continue;
		SubtractMoneyFromCompany(CompanyID{company}, CommandCost(ExpensesType::Property, cost));
	}
}

/** Daily cost of the emergency diesel generators. */
static void PayDiesel()
{
	std::map<uint8_t, Money> costs;
	for (const auto &[object, vent] : _vents) {
		const Object *o = GetPowerObject(object);
		if (o != nullptr && vent.diesel) costs[GetTileOwner(o->location.tile).base()] += _price[Price::TownAction] >> METRO_POWER_DIESEL_COST_SHIFT;
	}
	for (const auto &[company, cost] : costs) {
		if (!Company::IsValidID(CompanyID{company})) continue;
		SubtractMoneyFromCompany(CompanyID{company}, CommandCost(ExpensesType::Property, cost));
	}
}

static const IntervalTimer<TimerGameTick> _metro_power_ticks({TimerGameTick::Priority::None, METRO_POWER_UPDATE_TICKS}, [](auto) {
	UpdatePower();
});

static const IntervalTimer<TimerGameCalendar> _metro_power_daily({TimerGameCalendar::Trigger::Day, TimerGameCalendar::Priority::None}, [](auto) {
	ValidatePowerLines();
	MetroPowerMarkDirty(); // Industries open and close, objects change owners: cheap enough to rebuild once a day.
	UpdatePlantsDaily();
	PayDiesel();
});

static const IntervalTimer<TimerGameCalendar> _metro_power_monthly({TimerGameCalendar::Trigger::Month, TimerGameCalendar::Priority::None}, [](auto) {
	PayUpkeep();
});

void MetroPowerReset()
{
	_metro_power_lines.clear();
	_metro_power_plants.clear();
	_metro_power_companies.clear();
	_networks.clear();
	_object_network.clear();
	_industry_networks.clear();
	_substations.clear();
	_vents.clear();
	_train_power_percent.clear();
	_train_stop_news.clear();
	_company_blackout.clear();
	_topology_dirty = true;
}

void MetroPowerAfterLoad()
{
	_topology_dirty = true;
	_train_stop_news.clear();
	_company_blackout.clear();
	UpdatePower();
	/* Trains stopped without power before saving are already reported; do not repeat the news. */
	for (const auto &[train, percent] : _train_power_percent) {
		if (percent == 0) _train_stop_news.insert(train);
	}
}

/* ---------------------------------------------------------------------------------------------- */
/* Information for windows                                                                        */
/* ---------------------------------------------------------------------------------------------- */

/** Network of the grid building or power station on the tile, -1 if none. */
static int GetNetworkAt(TileIndex tile)
{
	if (const Object *o = GetPowerObjectAt(tile); o != nullptr) {
		auto it = _object_network.find(o->index.base());
		return it == _object_network.end() ? -1 : it->second;
	}
	if (IsTileType(tile, TileType::Industry)) {
		uint32_t ind = GetIndustryIndex(tile).base();
		for (size_t n = 0; n < _networks.size(); n++) {
			if (std::ranges::find(_networks[n].industries, ind) != _networks[n].industries.end()) return static_cast<int>(n);
		}
	}
	return -1;
}

/** Line of the network window describing a plant. */
static std::string GetPlantLine(const MetroPowerPlantState &state, StringID name, uint share)
{
	uint32_t capacity = GetPlantCapacityKw(state) / share;
	uint32_t nominal = GetPlantNominalKw(state) / share;
	if (GetPlantFuelPerMonth(state) == 0) return GetString(STR_METRO_POWER_PLANT_LINE, name, capacity, nominal);
	return GetString(STR_METRO_POWER_PLANT_FUEL_LINE, name, capacity, nominal, state.fuel);
}

MetroPowerNetworkInfo MetroPowerGetNetworkInfo(TileIndex tile)
{
	MetroPowerNetworkInfo info;
	if (_topology_dirty) UpdatePower();
	int n = GetNetworkAt(tile);
	if (n < 0) return info;
	const PowerNetwork &net = _networks[n];
	info.valid = true;
	info.owner = net.owner;
	info.capacity_kw = net.capacity_kw;
	info.nominal_kw = net.nominal_kw;
	info.vent_demand_kw = net.vent_demand_kw;
	info.track_demand_kw = net.track_demand_kw;
	info.supplied_kw = net.supplied_kw;
	info.line_tiles = net.line_tiles;
	info.tracks_first = IsTracksFirst(net.owner);
	for (uint32_t object : net.substations) {
		const SubstationState &state = _substations[object];
		info.substations++;
		if (!state.powered) info.substations_off++;
		info.trains += state.trains;
		info.trains_unpowered += state.trains_unpowered;
	}
	for (uint32_t object : net.fed_vents) {
		const VentState &vent = _vents[object];
		info.vents++;
		if (vent.diesel) info.vents_diesel++;
		if (!vent.grid && !vent.diesel) info.vents_off++;
	}
	for (uint32_t object : net.plants) {
		const Object *o = Object::Get(object);
		info.plants.push_back(GetPlantLine(GetObjectPlantState(o), GetMetroPowerBuildingName(GetBuilding(o)), 1));
	}
	for (uint32_t ind : net.industries) {
		const Industry *i = Industry::Get(ind);
		info.plants.push_back(GetPlantLine(GetIndustryPlantState(i), GetIndustrySpec(i->type)->name, std::max<uint>(_industry_networks[ind], 1)));
	}
	return info;
}

std::vector<std::string> MetroPowerGetBuildingLines(TileIndex tile)
{
	std::vector<std::string> lines;
	const Object *o = GetPowerObjectAt(tile);
	if (o == nullptr) return lines;
	MetroPowerBuilding building = GetBuilding(o);
	const MetroPowerBuildingSpec &spec = GetBuildingSpec(building);
	lines.push_back(GetString(spec.effect));
	switch (spec.role) {
		case MetroPowerRole::Plant: {
			const MetroPowerPlantState &state = GetObjectPlantState(o);
			if (spec.fuel != CT_INVALID) {
				CargoType fuel = GetCargoTypeByLabel(spec.fuel);
				if (IsValidCargoType(fuel)) lines.push_back(GetString(STR_METRO_POWER_FUEL_STOCK, CargoSpec::Get(fuel)->name, state.fuel, GetPlantCapacityKw(state)));
			}
			break;
		}

		case MetroPowerRole::Substation: {
			auto it = _substations.find(o->index.base());
			if (it != _substations.end()) {
				lines.push_back(GetString(it->second.powered ? STR_METRO_POWER_SUBSTATION_ON : STR_METRO_POWER_SUBSTATION_OFF, it->second.demand_kw, it->second.trains));
			}
			break;
		}

		case MetroPowerRole::Vent: {
			auto it = _vents.find(o->index.base());
			StringID str = STR_METRO_POWER_VENT_OFF;
			if (it != _vents.end() && it->second.grid) str = STR_METRO_POWER_VENT_GRID;
			if (it != _vents.end() && it->second.diesel) str = STR_METRO_POWER_VENT_DIESEL;
			lines.push_back(GetString(str, spec.power_kw));
			break;
		}
	}
	if (_object_network.find(o->index.base()) == _object_network.end()) lines.push_back(GetString(STR_METRO_POWER_NOT_CONNECTED));
	return lines;
}

std::vector<std::string> MetroPowerGetIndustryLines(const Industry *i)
{
	std::vector<std::string> lines;
	if (!IsMetroPowerStationIndustry(i)) return lines;
	const MetroPowerPlantState *state = FindPlantState(1, i->index.base());
	uint32_t fuel = state == nullptr ? 0 : state->fuel;
	uint32_t capacity = state == nullptr ? 0 : GetPlantCapacityKw(*state);
	lines.push_back(GetString(STR_METRO_POWER_INDUSTRY_STOCK, fuel, capacity, METRO_POWER_COAL_STATION_KW));
	lines.push_back(GetString(_industry_networks.contains(i->index.base()) ? STR_METRO_POWER_INDUSTRY_CONNECTED : STR_METRO_POWER_INDUSTRY_NOT_CONNECTED));
	return lines;
}

bool MetroPowerClickTile(TileIndex tile)
{
	if (GetPowerObjectAt(tile) == nullptr) return false;
	ShowMetroPowerNetworkWindow(tile);
	return true;
}

/* ---------------------------------------------------------------------------------------------- */
/* Drawing                                                                                        */
/* ---------------------------------------------------------------------------------------------- */

/** Whether the line on @p tile connects towards @p next: another line, a grid building or a power station. */
static bool LineConnects(TileIndex next, uint8_t owner)
{
	if (!IsValidTile(next)) return false;
	auto it = _metro_power_lines.find(next.base());
	if (it != _metro_power_lines.end()) return it->second == owner;
	if (const Object *o = GetPowerObjectAt(next); o != nullptr) return GetTileOwner(next).base() == owner;
	return IsTileType(next, TileType::Industry) && IsMetroPowerStationIndustry(Industry::GetByTile(next));
}

/* Offsets of the catenary sprites (see table/elrail_data.h): wires of a flat tile and pylons. */
static constexpr uint8_t METRO_WIRE_X_SHORT = 0;
static constexpr uint8_t METRO_WIRE_Y_SHORT = 1;
static constexpr uint8_t METRO_PYLON_Y_NE = 0;
static constexpr uint8_t METRO_PYLON_X_NW = 2;
static constexpr int METRO_LINE_WIRE_HEIGHT = 10; ///< Height of the wires above the ground (as catenary).

/** Power lines drawn with the vanilla catenary pylons and wires (temporary sprites). */
void MetroPowerDrawTile(const TileInfo *ti)
{
	if (_metro_power_lines.empty()) return;
	auto it = _metro_power_lines.find(ti->tile.base());
	if (it == _metro_power_lines.end()) return;
	uint8_t owner = it->second;

	bool x = LineConnects(TileAddByDiagDir(ti->tile, DiagDirection::NE), owner) || LineConnects(TileAddByDiagDir(ti->tile, DiagDirection::SW), owner);
	bool y = LineConnects(TileAddByDiagDir(ti->tile, DiagDirection::NW), owner) || LineConnects(TileAddByDiagDir(ti->tile, DiagDirection::SE), owner);
	if (!x && !y) x = true;

	int z = GetTileMaxPixelZ(ti->tile);
	bool transparent = IsTransparencySet(TransparencyOption::Catenary);
	if (x) {
		AddSortableSpriteToDraw(SPR_WIRE_BASE + METRO_WIRE_X_SHORT, PAL_NONE, ti->x, ti->y, z, SpriteBounds({0, 7, METRO_LINE_WIRE_HEIGHT}, {15, 1, 1}, {}), transparent);
		AddSortableSpriteToDraw(SPR_PYLON_BASE + METRO_PYLON_X_NW, PAL_NONE, ti->x + 8, ti->y + 6, z, SpriteBounds({-1, -1, 0}, {1, 1, 10}, {1, 1, 0}), transparent);
	}
	if (y) {
		AddSortableSpriteToDraw(SPR_WIRE_BASE + METRO_WIRE_Y_SHORT, PAL_NONE, ti->x, ti->y, z, SpriteBounds({7, 0, METRO_LINE_WIRE_HEIGHT}, {1, 15, 1}, {}), transparent);
		AddSortableSpriteToDraw(SPR_PYLON_BASE + METRO_PYLON_Y_NE, PAL_NONE, ti->x + 10, ti->y + 8, z, SpriteBounds({-1, -1, 0}, {1, 1, 10}, {1, 1, 0}), transparent);
	}
}

/* ---------------------------------------------------------------------------------------------- */
/* Console                                                                                        */
/* ---------------------------------------------------------------------------------------------- */

/** Company that builds for console commands: the local one, or the first company when there is none (headless tests). */
static CompanyID GetConsoleCompany()
{
	if (Company::IsValidID(_local_company)) return _local_company;
	for (const Company *c : Company::Iterate()) return c->index;
	return CompanyID::Invalid();
}

static void PrintConsoleResult(const CommandCost &ret)
{
	if (ret.Succeeded()) {
		IConsolePrint(CC_INFO, "Done, cost {}.", ret.GetCost());
	} else {
		IConsolePrint(CC_ERROR, "Failed: {}", GetString(ret.GetErrorMessage()));
	}
}

bool ConMetroPower(std::span<std::string_view> argv)
{
	if (argv.empty()) {
		IConsolePrint(CC_HELP, "Metro mod power grid. Usage: metro_power status | networks | build <0-5> <x> <y> | line <x1> <y1> <x2> <y2> | fuel <x> <y> <amount> | fuel all <amount> | stations | trains | priority vents|tracks | testtrain <x> <y>");
		IConsolePrint(CC_HELP, "  build: 0 substation, 1 gas, 2 nuclear, 3 solar, 4 wind, 5 vent shaft; fuel: add fuel to the plant or power station at the tile. Console builds are for tests: no money check.");
		return true;
	}
	std::string_view cmd = argv.size() > 1 ? argv[1] : "status";
	auto arg = [&](size_t i) { return i < argv.size() ? ParseInteger<uint>(argv[i]) : std::nullopt; };

	if (cmd == "status" || cmd == "networks") {
		UpdatePower();
		IConsolePrint(CC_INFO, "lines={} plants={} networks={} trains with less power={} setting trains_need_grid={}", _metro_power_lines.size(), _metro_power_plants.size(),
				_networks.size(), _train_power_percent.size(), _settings_game.vehicle.metro_trains_need_grid ? "on" : "off");
		for (size_t n = 0; n < _networks.size(); n++) {
			const PowerNetwork &net = _networks[n];
			IConsolePrint(CC_DEFAULT, "#{} company {}: lines {}, plants {}, power stations {}, substations {}, vents {}; capacity {} kW of {}, vents {} kW, tracks {} kW, fed {} kW{}",
					n, net.owner.base(), net.line_tiles, net.plants.size(), net.industries.size(), net.substations.size(), net.fed_vents.size(),
					net.capacity_kw, net.nominal_kw, net.vent_demand_kw, net.track_demand_kw, net.supplied_kw, IsTracksFirst(net.owner) ? ", tracks first" : "");
			std::string where;
			for (const auto *list : {&net.plants, &net.substations, &net.vents}) {
				for (uint32_t object : *list) {
					TileIndex t = Object::Get(object)->location.tile;
					where += fmt::format(" {}@({},{})", GetString(GetMetroPowerBuildingName(GetBuilding(Object::Get(object)))), TileX(t), TileY(t));
				}
			}
			if (!where.empty()) IConsolePrint(CC_DEFAULT, "   {}", where);
		}
		return true;
	}
	if (cmd == "stations") {
		UpdatePower();
		for (const Industry *i : Industry::Iterate()) {
			if (!IsMetroPowerStationIndustry(i)) continue;
			const MetroPowerPlantState *state = FindPlantState(1, i->index.base());
			IConsolePrint(CC_DEFAULT, "power station {} at ({}, {}) size {}x{}: coal {}, output {} kW, networks {}", i->index, TileX(i->location.tile), TileY(i->location.tile),
					i->location.w, i->location.h, state == nullptr ? 0 : state->fuel, state == nullptr ? 0 : GetPlantCapacityKw(*state),
					_industry_networks.contains(i->index.base()) ? _industry_networks[i->index.base()] : 0);
		}
		return true;
	}
	if (cmd == "trains") {
		UpdatePower();
		for (const Train *front : Train::Iterate()) {
			if (!front->IsFrontEngine()) continue;
			bool electric = false;
			for (const Train *u = front; u != nullptr; u = u->Next()) electric |= GetPartPower(u) > 0 && NeedsGrid(u);
			if (!electric) continue;
			IConsolePrint(CC_DEFAULT, "train {} (company {}): {}% power, speed {}, tile ({}, {}){}", front->index, front->owner.base(), MetroPowerGetTrainPowerPercent(front),
					front->cur_speed, TileX(front->tile), TileY(front->tile), front->vehstatus.Test(VehState::Crashed) ? ", crashed" : "");
		}
		return true;
	}

	CompanyID company = GetConsoleCompany();
	if (company == CompanyID::Invalid()) {
		IConsolePrint(CC_ERROR, "No company.");
		return true;
	}
	if (cmd == "window") {
		ShowMetroPowerToolbar();
		return true;
	}
	if (cmd == "build" && argv.size() > 4) {
		auto type = arg(2), x = arg(3), y = arg(4);
		if (!type.has_value() || *type >= METRO_POWER_BUILDING_COUNT || !x.has_value() || !y.has_value() || *x >= Map::SizeX() || *y >= Map::SizeY()) {
			IConsolePrint(CC_ERROR, "Invalid arguments.");
			return true;
		}
		Backup<CompanyID> cur_company(_current_company, company, FILE_LINE);
		CommandCost ret = Command<Commands::MetroBuildPowerObject>::Do(DoCommandFlags{DoCommandFlag::Execute, DoCommandFlag::Bankrupt}, TileXY(*x, *y), static_cast<uint8_t>(*type));
		cur_company.Restore();
		PrintConsoleResult(ret);
		return true;
	}
	if (cmd == "line" && argv.size() > 5) {
		auto x1 = arg(2), y1 = arg(3), x2 = arg(4), y2 = arg(5);
		if (!x1 || !y1 || !x2 || !y2 || *x1 >= Map::SizeX() || *x2 >= Map::SizeX() || *y1 >= Map::SizeY() || *y2 >= Map::SizeY()) {
			IConsolePrint(CC_ERROR, "Invalid arguments.");
			return true;
		}
		Backup<CompanyID> cur_company(_current_company, company, FILE_LINE);
		CommandCost ret = Command<Commands::MetroBuildPowerLine>::Do(DoCommandFlags{DoCommandFlag::Execute, DoCommandFlag::Bankrupt}, TileXY(*x2, *y2), TileXY(*x1, *y1), false);
		cur_company.Restore();
		PrintConsoleResult(ret);
		return true;
	}
	if (cmd == "priority" && argv.size() > 2) {
		Backup<CompanyID> cur_company(_current_company, company, FILE_LINE);
		PrintConsoleResult(Command<Commands::MetroSetPowerPriority>::Do(DoCommandFlag::Execute, argv[2] == "tracks"));
		cur_company.Restore();
		return true;
	}
	if (cmd == "testtrain" && argv.size() > 3) {
		/* For tests: a straight electrified track of 30 tiles going +x from (x, y), a depot at its west end and a running electric engine. */
		auto x = arg(2), y = arg(3);
		if (!x || !y || *x < 2 || *x + 32 >= Map::SizeX() || *y >= Map::SizeY()) {
			IConsolePrint(CC_ERROR, "Invalid arguments.");
			return true;
		}
		RailType rt = INVALID_RAILTYPE;
		for (RailType r = RAILTYPE_BEGIN; r < RAILTYPE_END; r = static_cast<RailType>(r + 1)) {
			if (GetRailTypeInfo(r)->flags.Test(RailTypeFlag::Catenary) && HasRailTypeAvail(company, r)) {
				rt = r;
				break;
			}
		}
		const Engine *engine = nullptr;
		for (const Engine *e : Engine::IterateType(VehicleType::Train)) {
			if (!IsEngineBuildable(e->index, VehicleType::Train, company) || e->VehInfo<RailVehicleInfo>().power == 0) continue;
			RailTypes types = e->VehInfo<RailVehicleInfo>().railtypes;
			if (types.None() || !types.Test(rt)) continue;
			bool catenary = true;
			for (RailType r : types) catenary &= GetRailTypeInfo(r)->flags.Test(RailTypeFlag::Catenary);
			if (catenary) {
				engine = e;
				break;
			}
		}
		if (rt == INVALID_RAILTYPE || engine == nullptr) {
			IConsolePrint(CC_ERROR, "No electrified rail type or electric engine available this year.");
			return true;
		}
		Backup<CompanyID> cur_company(_current_company, company, FILE_LINE);
		/* Look for a free row (no roads or other obstacles) from y southwards. */
		CommandCost ret = CMD_ERROR;
		uint ty = *y;
		for (; ty < Map::SizeY() - 1 && ty < *y + 60; ty++) {
			DoCommandFlags test{DoCommandFlag::Bankrupt};
			if (Command<Commands::BuildRailLong>::Do(test, TileXY(*x + 29, ty), TileXY(*x, ty), rt, TRACK_X, BuildRailTrackFlags{}, true).Failed()) continue;
			if (Command<Commands::BuildRailDepot>::Do(test, TileXY(*x - 1, ty), rt, DiagDirection::SW).Failed()) continue;
			ret = CommandCost();
			break;
		}
		TileIndex start = TileXY(*x, ty);
		TileIndex depot = TileXY(*x - 1, ty);
		if (ret.Succeeded()) ret = Command<Commands::BuildRailLong>::Do(DoCommandFlags{DoCommandFlag::Execute, DoCommandFlag::Bankrupt}, TileXY(*x + 29, ty), start, rt, TRACK_X, BuildRailTrackFlags{}, true);
		if (ret.Succeeded()) ret = Command<Commands::BuildRailDepot>::Do(DoCommandFlags{DoCommandFlag::Execute, DoCommandFlag::Bankrupt}, depot, rt, DiagDirection::SW);
		std::optional<VehicleID> vehicle;
		if (ret.Succeeded()) {
			ret = Command<Commands::BuildVehicle>::Do(DoCommandFlag::Execute, depot, engine->index, true, INVALID_CARGO, ClientID::Invalid);
			vehicle = ret.GetResultData<VehicleID>();
		}
		if (ret.Succeeded() && vehicle.has_value()) ret = Command<Commands::StartStopVehicle>::Do(DoCommandFlag::Execute, *vehicle, false);
		cur_company.Restore();
		if (ret.Failed()) {
			PrintConsoleResult(ret);
		} else {
			IConsolePrint(CC_INFO, "Train {} ({}) started on track ({}, {})..({}, {}).", vehicle.value_or(VehicleID::Invalid()), GetString(STR_ENGINE_NAME, engine->index), *x, ty, *x + 29, ty);
		}
		return true;
	}
	if (cmd == "fuel" && argv.size() == 4 && argv[2] == "all") {
		auto amount = arg(3);
		uint count = 0;
		for (const Object *o : Object::Iterate()) {
			if (!IsMetroPowerObject(o->type) || GetBuildingSpec(GetBuilding(o)).role != MetroPowerRole::Plant || GetTileOwner(o->location.tile) != company) continue;
			MetroPowerPlantState &state = GetObjectPlantState(o);
			state.fuel = std::min<uint32_t>(state.fuel + amount.value_or(0), METRO_POWER_FUEL_STOCK_MAX);
			state.output = 1000;
			count++;
		}
		IConsolePrint(CC_INFO, "Fuel added to {} plants.", count);
		return true;
	}
	if (cmd == "fuel" && argv.size() > 4) {
		auto x = arg(2), y = arg(3), amount = arg(4);
		if (!x || !y || !amount || *x >= Map::SizeX() || *y >= Map::SizeY()) {
			IConsolePrint(CC_ERROR, "Invalid arguments.");
			return true;
		}
		TileIndex tile = TileXY(*x, *y);
		MetroPowerPlantState *state = nullptr;
		if (const Object *o = GetPowerObjectAt(tile); o != nullptr && GetBuildingSpec(GetBuilding(o)).role == MetroPowerRole::Plant) state = &GetObjectPlantState(o);
		if (IsTileType(tile, TileType::Industry) && IsMetroPowerStationIndustry(Industry::GetByTile(tile))) state = &GetIndustryPlantState(Industry::GetByTile(tile));
		if (state == nullptr) {
			IConsolePrint(CC_ERROR, "No plant at that tile.");
			return true;
		}
		state->fuel = std::min<uint32_t>(state->fuel + *amount, METRO_POWER_FUEL_STOCK_MAX);
		state->output = 1000;
		IConsolePrint(CC_INFO, "Fuel in stock: {}.", state->fuel);
		return true;
	}
	IConsolePrint(CC_ERROR, "Unknown subcommand.");
	return true;
}
