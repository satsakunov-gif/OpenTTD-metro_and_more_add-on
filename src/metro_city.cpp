/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_city.cpp Metro mod: active towns, town services, tourists and stadium matches.
 *
 * A town becomes active for good with the first transport in it: when a station of the player in its local authority zone
 * sends or receives at least one unit of passengers or cargo. Building a station alone does not activate the town.
 * Everything of this file (services, tourists, matches) and the AI rating work only in active towns.
 */

#include "stdafx.h"
#include "metro_city.h"
#include "metro_city_cmd.h"
#include "metro_town.h"
#include "bridge_map.h"
#include "cargotype.h"
#include "clear_map.h"
#include "date_func.h"
#include "effectvehicle_base.h"
#include "effectvehicle_func.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "console_func.h"
#include "console_type.h"
#include "economy_func.h"
#include "gfx_func.h"
#include "window_gui.h"
#include "landscape_cmd.h"
#include "news_func.h"
#include "newgrf_house.h"
#include "object.h"
#include "object_base.h"
#include "object_map.h"
#include "road_map.h"
#include "settings_type.h"
#include "station_base.h"
#include "station_container.h"
#include "strings_func.h"
#include "tile_cmd.h"
#include "town.h"
#include "linkgraph/linkgraph.h"
#include "tunnelbridge.h"
#include "tunnelbridge_map.h"
#include "vehicle_base.h"
#include "viewport_func.h"
#include "window_func.h"
#include "core/backup_type.hpp"
#include "core/random_func.hpp"
#include "core/string_consumer.hpp"
#include "timer/timer.h"
#include "timer/timer_game_calendar.h"
#include "timer/timer_game_tick.h"
#include "table/metro_city_tables.h"
#include "table/strings.h"

#include <deque>
#include <unordered_map>

#include "safeguards.h"

std::map<uint16_t, MetroCityTownState> _metro_city_towns;
bool _metro_show_landmarks = false;
static std::map<uint16_t, TileIndex> _metro_stadiums; ///< Stadium of each active town, refreshed daily; not saved.

/* ---------------------------------------------------------------------------------------------- */
/* Active towns                                                                                   */
/* ---------------------------------------------------------------------------------------------- */

static MetroCityTownState &GetTownState(TownID town)
{
	auto [it, inserted] = _metro_city_towns.try_emplace(town.base());
	if (inserted) it->second.town = town.base();
	return it->second;
}

const MetroCityTownState *MetroCityGetTownState(const Town *t)
{
	auto it = _metro_city_towns.find(t->index.base());
	return it == _metro_city_towns.end() ? nullptr : &it->second;
}

bool MetroCityIsTownActive(TownID town)
{
	auto it = _metro_city_towns.find(town.base());
	return it != _metro_city_towns.end() && it->second.active;
}

bool MetroCityIsTownActive(const Town *t)
{
	return t != nullptr && MetroCityIsTownActive(t->index);
}

/** Town whose local authority zone contains the tile, or nullptr. */
static Town *GetLocalAuthorityTown(TileIndex tile)
{
	return ClosestTownFromTile(tile, _settings_game.economy.dist_local_authority);
}

/** Activate the town for good. */
static void ActivateTown(const Town *t)
{
	MetroCityTownState &state = GetTownState(t->index);
	if (state.active) return;
	state.active = true;
	state.active_since = CalTime::CurDate().base();
	InvalidateWindowData(WindowClass::TownView, t->index);
	SetWindowDirty(WindowClass::TownAuthority, t->index);
}

/**
 * Cargo was really moved at the station: loaded onto a vehicle or delivered to it (at least one unit).
 * The first such transport of a human player activates the town whose local authority zone contains the station.
 */
void MetroCityOnCargoMoved(const Station *st)
{
	if (!Company::IsValidHumanID(st->owner)) return;
	Town *t = GetLocalAuthorityTown(st->xy);
	if (t != nullptr && !MetroCityIsTownActive(t)) ActivateTown(t);
}

void MetroCityReset()
{
	_metro_city_towns.clear();
	_metro_stadiums.clear();
}

void MetroCityOnTownDeleted(TownID town)
{
	_metro_city_towns.erase(town.base());
}

static TileIndex FindTownStadium(const Town *t);
static void UpdateMatches();
static void TickStadiumAnimation();
static void RollTouristMonth();
static void DrawLandmarkFrame(const TileInfo *ti);

static const IntervalTimer<TimerGameCalendar> _metro_city_daily({TimerGameCalendar::Trigger::Day, TimerGameCalendar::Priority::None}, [](auto) {
	UpdateMatches();
});

/* ---------------------------------------------------------------------------------------------- */
/* Services                                                                                       */
/* ---------------------------------------------------------------------------------------------- */

static_assert(OBJECT_METRO_HOSPITAL == OBJECT_METRO_SCHOOL + to_underlying(MetroServiceType::Hospital));
static_assert(OBJECT_METRO_UNIVERSITY == OBJECT_METRO_SCHOOL + to_underlying(MetroServiceType::University));
static_assert(METRO_SERVICE_COUNT <= METRO_OBJECT_COUNT);

static const MetroServiceSpec &GetServiceSpec(MetroServiceType type)
{
	return _metro_service_specs[to_underlying(type)];
}

bool IsMetroServiceObject(ObjectType type)
{
	return type >= OBJECT_METRO_SCHOOL && type < OBJECT_METRO_SCHOOL + METRO_SERVICE_COUNT;
}

MetroServiceType GetMetroServiceTypeOfObject(ObjectType type)
{
	return static_cast<MetroServiceType>(type - OBJECT_METRO_SCHOOL);
}

StringID GetMetroServiceName(MetroServiceType type)
{
	return GetServiceSpec(type).name;
}

Money GetMetroServiceCost(MetroServiceType type)
{
	return _price[Price::TownAction] * GetServiceSpec(type).cost_factor >> 8;
}

uint32_t GetMetroServiceMinPopulation(MetroServiceType type)
{
	return GetServiceSpec(type).min_population;
}

/** Service object on the tile, or nullptr. */
static const Object *GetServiceObject(TileIndex tile)
{
	if (!IsTileType(tile, TileType::Object)) return nullptr;
	const Object *o = Object::GetByTile(tile);
	return IsMetroServiceObject(o->type) ? o : nullptr;
}

uint CountMetroServices(const Town *t, MetroServiceType type)
{
	uint count = 0;
	ObjectType object = GetServiceSpec(type).object;
	for (const Object *o : Object::Iterate()) {
		if (o->type == object && o->town == t) count++;
	}
	return count;
}

bool MetroCityGetServiceInfo(TileIndex tile, MetroServiceType &type, TownID &town, int32_t &build_date)
{
	if (const Object *o = GetServiceObject(tile); o != nullptr) {
		type = GetMetroServiceTypeOfObject(o->type);
		town = o->town->index;
		build_date = o->build_date.base();
		return true;
	}
	return false;
}

bool MetroCityClickTile(TileIndex tile)
{
	MetroServiceType type;
	TownID town;
	int32_t build_date;
	if (!MetroCityGetServiceInfo(tile, type, town, build_date)) return false;
	ShowMetroServiceWindow(tile);
	return true;
}

/** Whether a road tile is next to the tile. */
static bool HasRoadNextTo(TileIndex tile)
{
	for (DiagDirection dir : EnumRange(DiagDirection::End)) {
		TileIndex adj = TileAddByDiagDir(tile, dir);
		if (IsValidTile(adj) && IsTileType(adj, TileType::Road) && !IsRoadDepot(adj)) return true;
	}
	return false;
}

std::vector<std::string> MetroCityGetServiceLines(TileIndex tile)
{
	std::vector<std::string> lines;
	MetroServiceType type;
	TownID town;
	int32_t build_date;
	if (!MetroCityGetServiceInfo(tile, type, town, build_date)) return lines;

	const MetroServiceSpec &spec = GetServiceSpec(type);
	lines.push_back(GetString(STR_METRO_SERVICE_BUILT, CalTime::Date{build_date}));
	lines.push_back(GetString(spec.effect));
	if (!MetroCityIsTownActive(town)) lines.push_back(GetString(STR_METRO_SERVICE_TOWN_INACTIVE));
	if (type == MetroServiceType::Hospital && !HasRoadNextTo(tile)) lines.push_back(GetString(STR_METRO_SERVICE_NO_ROAD));
	return lines;
}

extern CommandCost CheckBuildableTile(TileIndex tile, DiagDirections invalid_dirs, int &allowed_z, bool allow_steep, bool check_bridge);

/**
 * Build a town service.
 * @param flags Type of operation.
 * @param tile Tile to build on.
 * @param service_type #MetroServiceType to build.
 * @return The cost of this operation or an error.
 */
CommandCost CmdMetroBuildService(DoCommandFlags flags, TileIndex tile, uint8_t service_type)
{
	if (service_type >= METRO_SERVICE_COUNT || !Company::IsValidID(_current_company)) return CMD_ERROR;
	MetroServiceType type = static_cast<MetroServiceType>(service_type);
	const MetroServiceSpec &spec = GetServiceSpec(type);

	Town *t = GetLocalAuthorityTown(tile);
	if (t == nullptr) return CommandCost(STR_METRO_CITY_ERR_NO_TOWN);
	if (!MetroCityIsTownActive(t)) return CommandCost(STR_METRO_CITY_ERR_INACTIVE);
	if (t->cache.population < spec.min_population) return CommandCostWithParam(STR_METRO_CITY_ERR_POPULATION, spec.min_population);

	CommandCost cost(ExpensesType::Construction, GetMetroServiceCost(type));

	if (!Object::CanAllocateItem()) return CommandCost(STR_ERROR_TOO_MANY_OBJECTS);
	if (IsBridgeAbove(tile)) return CommandCost(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);

	/* Services stand on free land, not on roads of the town. */
	if (IsTileType(tile, TileType::Road)) return CommandCost(STR_ERROR_SITE_UNSUITABLE);
	CommandCost ret = Command<Commands::LandscapeClear>::Do(DoCommandFlags{flags}.Reset(DoCommandFlag::Execute), tile);
	if (ret.Failed()) return ret;
	cost.AddCost(std::move(ret));

	auto [slope, allowed_z] = GetTileSlopeZ(tile);
	if (slope != SLOPE_FLAT) allowed_z++;
	ret = CheckBuildableTile(tile, {}, allowed_z, false, false);
	if (ret.Failed()) return ret;

	if (flags.Test(DoCommandFlag::Execute)) {
		Command<Commands::LandscapeClear>::Do(flags, tile);
		BuildObject(spec.object, tile, _current_company, t);
		InvalidateWindowData(WindowClass::TownView, t->index);
		InvalidateWindowData(WindowClass::MetroTownServices, t->index);
	}
	return cost;
}

bool MetroCityAddAcceptedCargo(TileIndex tile, CargoArray &acceptance, CargoTypes &always_accepted)
{
	const Object *o = GetServiceObject(tile);
	if (o == nullptr) return false;
	if (GetServiceSpec(GetMetroServiceTypeOfObject(o->type)).passengers == 0) return true;

	CargoType pass = GetCargoTypeByLabel(CT_PASSENGERS);
	if (IsValidCargoType(pass)) {
		acceptance[pass] += 8;
		always_accepted.Set(pass);
	}
	return true;
}

bool MetroCityAddProducedCargo(TileIndex tile, CargoArray &produced)
{
	const Object *o = GetServiceObject(tile);
	if (o == nullptr) return false;
	if (GetServiceSpec(GetMetroServiceTypeOfObject(o->type)).passengers == 0) return true;

	CargoType pass = GetCargoTypeByLabel(CT_PASSENGERS);
	if (IsValidCargoType(pass)) produced[pass]++;
	return true;
}

/** Pupils, patients and students: passengers from the services of active towns to the stations around them. */
static void ProduceServicePassengers()
{
	CargoType pass = GetCargoTypeByLabel(CT_PASSENGERS);
	if (!IsValidCargoType(pass)) return;

	for (const Object *o : Object::Iterate()) {
		if (!IsMetroServiceObject(o->type) || !MetroCityIsTownActive(o->town)) continue;
		uint amount = GetServiceSpec(GetMetroServiceTypeOfObject(o->type)).passengers;
		if (amount == 0) continue;
		StationFinder stations(o->location);
		MoveGoodsToStation(pass, amount, Source::Make<SourceType::Town>(o->town->index), stations.GetStations());
	}
}

uint MetroCityAdjustGrowthRate(const Town *t, uint rate)
{
	if (!MetroCityIsTownActive(t)) return rate;
	if (t->cache.population >= GetMetroServiceMinPopulation(MetroServiceType::School) && CountMetroServices(t, MetroServiceType::School) == 0) {
		rate = rate * METRO_GROWTH_NO_SCHOOL_PERCENT / 100;
	}
	if (CountMetroServices(t, MetroServiceType::University) > 0) rate = rate * METRO_GROWTH_UNIVERSITY_PERCENT / 100;
	return MetroTownAdjustGrowthRate(t, std::max(rate, 1u)); // Migration: towns losing people grow slower.
}

/** A hospital without a road next to it lowers the town rating of its owner. */
static void CheckHospitalAccess()
{
	for (const Object *o : Object::Iterate()) {
		if (o->type != OBJECT_METRO_HOSPITAL || !MetroCityIsTownActive(o->town) || HasRoadNextTo(o->location.tile)) continue;
		Owner owner = GetTileOwner(o->location.tile);
		if (!Company::IsValidID(owner)) continue;
		Backup<CompanyID> cur_company(_current_company, owner, FILE_LINE);
		ChangeTownRating(o->town, METRO_HOSPITAL_NO_ROAD_RATING, METRO_HOSPITAL_NO_ROAD_MIN_RATING, DoCommandFlags{DoCommandFlag::Execute});
		cur_company.Restore();
	}
}

void MetroCityDrawTile(const TileInfo *ti)
{
	if (_metro_show_landmarks) DrawLandmarkFrame(ti);
}

/**
 * Whether the tile is shown in a viewport (the main one or an extra one). Animations and effects run only for such tiles.
 * @param tile Tile to check.
 * @param margin Extra virtual pixels around the tile, for tall buildings and effects above them.
 */
static bool IsTileVisible(TileIndex tile, int margin = 64)
{
	if (IsHeadless()) return false;
	Point pt = RemapCoords2(TileX(tile) * TILE_SIZE + TILE_SIZE / 2, TileY(tile) * TILE_SIZE + TILE_SIZE / 2);
	for (const Window *w : Window::IterateFromBack()) {
		const Viewport *vp = w->viewport;
		if (vp == nullptr) continue;
		if (pt.x + margin >= vp->virtual_left && pt.x - margin < vp->virtual_left + vp->virtual_width &&
				pt.y + margin >= vp->virtual_top && pt.y - 2 * margin < vp->virtual_top + vp->virtual_height) {
			return true;
		}
	}
	return false;
}

void MetroCityToggleLandmarks()
{
	_metro_show_landmarks = !_metro_show_landmarks;
	MarkWholeScreenDirty();
}

static const IntervalTimer<TimerGameTick> _metro_city_ticks({TimerGameTick::Priority::None, 1}, [](auto) {
	TickStadiumAnimation();
});

static const IntervalTimer<TimerGameTick> _metro_city_service_ticks({TimerGameTick::Priority::None, 256}, [](auto) {
	ProduceServicePassengers();
});

static const IntervalTimer<TimerGameCalendar> _metro_city_monthly({TimerGameCalendar::Trigger::Month, TimerGameCalendar::Priority::None}, [](auto) {
	CheckHospitalAccess();
	RollTouristMonth();
});

/* ---------------------------------------------------------------------------------------------- */
/* Landmarks, tourists and stadium matches                                                        */
/* ---------------------------------------------------------------------------------------------- */

/** Whether the tile is part of a landmark house: church, stadium, statue, fountain, park, theatre or cinema. */
static bool IsLandmarkTile(TileIndex tile)
{
	if (!IsTileType(tile, TileType::House)) return false;
	HouseID house = GetHouseType(tile);
	const HouseSpec *hs = HouseSpec::Get(house);
	if (hs->building_flags.Any({BuildingFlag::IsChurch, BuildingFlag::IsStadium})) return true;
	HouseID original = house < NEW_HOUSE_OFFSET ? house : hs->grf_prop.subst_id;
	if (original >= NEW_HOUSE_OFFSET) return false;
	return std::ranges::find(_metro_landmark_house_names, HouseSpec::Get(original)->building_name) != std::end(_metro_landmark_house_names);
}

/** Whether a landmark of an active town lies in the catchment of the station. */
static bool IsStationNearLandmark(const Station *st)
{
	for (TileIndex tile : st->catchment_tiles) {
		if (IsLandmarkTile(tile) && MetroCityIsTownActive(GetTownIndex(tile))) return true;
	}
	return false;
}

/** North tile of the stadium of the town, or INVALID_TILE. */
static TileIndex FindTownStadium(const Town *t)
{
	if (t->stadium_count == 0) return INVALID_TILE;
	uint radius = std::max<uint>(4, IntSqrt(t->cache.squared_town_zone_radius[to_underlying(HouseZone::TownEdge)]) + 2);
	for (TileIndex tile : SpiralTileSequence(t->xy, 2 * radius + 1)) {
		if (!IsTileType(tile, TileType::House) || GetTownIndex(tile) != t->index) continue;
		if (HouseSpec::Get(GetHouseType(tile))->building_flags.Test(BuildingFlag::IsStadium)) return tile;
	}
	return INVALID_TILE;
}

/** Towns connected to @p t by the passenger network of the human players (same link graph). */
static std::vector<TownID> GetConnectedTowns(const Town *t)
{
	std::vector<TownID> towns;
	std::vector<LinkGraphID> graphs;
	for (const Station *st : Station::Iterate()) {
		if (st->town != t || !Company::IsValidHumanID(st->owner)) continue;
		for (CargoType cargo : CargoSpec::town_production_cargo_mask[TownProductionEffect::Passengers]) {
			LinkGraphID lg = st->goods[cargo].link_graph;
			if (lg != LinkGraphID::Invalid() && std::ranges::find(graphs, lg) == graphs.end()) graphs.push_back(lg);
		}
	}
	for (LinkGraphID lg : graphs) {
		if (!LinkGraph::IsValidID(lg)) continue;
		const LinkGraph &graph = *LinkGraph::Get(lg);
		for (NodeID node = 0; node < graph.Size(); node++) {
			const Station *other = Station::GetIfValid(graph[node].Station());
			if (other == nullptr || other->town == t) continue;
			if (std::ranges::find(towns, other->town->index) == towns.end()) towns.push_back(other->town->index);
		}
	}
	return towns;
}

/** Towns whose fans travel to a match of the level; may lower the level when there are no such towns. */
static std::vector<TownID> GetFanTowns(const Town *t, MetroMatchLevel &level)
{
	if (level == MetroMatchLevel::Final) {
		std::vector<TownID> towns = GetConnectedTowns(t);
		if (!towns.empty()) {
			towns.insert(towns.begin(), t->index);
			return towns;
		}
		level = MetroMatchLevel::Regional;
	}
	if (level == MetroMatchLevel::Regional) {
		std::vector<std::pair<uint, TownID>> near;
		for (const Town *other : Town::Iterate()) {
			uint distance = DistanceManhattan(t->xy, other->xy);
			if (other != t && distance <= METRO_MATCH_REGION_TILES) near.emplace_back(distance, other->index);
		}
		if (!near.empty()) {
			std::sort(near.begin(), near.end());
			std::vector<TownID> towns{t->index};
			for (const auto &[distance, town] : near) towns.push_back(town);
			return towns;
		}
		level = MetroMatchLevel::Local;
	}
	return {t->index};
}

/** Town names for the news, at most #METRO_MATCH_MAX_FAN_TOWNS of them. */
static std::string GetFanTownNames(const std::vector<TownID> &towns)
{
	std::string names;
	for (size_t i = 0; i < towns.size() && i < METRO_MATCH_MAX_FAN_TOWNS; i++) {
		if (!names.empty()) names += ", ";
		names += GetString(STR_TOWN_NAME, towns[i]);
	}
	if (towns.size() > METRO_MATCH_MAX_FAN_TOWNS) names += ", ...";
	return names;
}

/** Pick a random match level by the weights of the table. */
static MetroMatchLevel RandomMatchLevel()
{
	uint total = 0;
	for (const MetroMatchSpec &spec : _metro_match_specs) total += spec.weight;
	uint pick = RandomRange(total);
	for (uint i = 0; i < std::size(_metro_match_specs); i++) {
		if (pick < _metro_match_specs[i].weight) return static_cast<MetroMatchLevel>(i);
		pick -= _metro_match_specs[i].weight;
	}
	return MetroMatchLevel::Local;
}

/** Plan, announce and play the stadium matches of the active towns. */
static void UpdateMatches()
{
	int32_t today = CalTime::CurDate().base();
	for (Town *t : Town::Iterate()) {
		if (!MetroCityIsTownActive(t)) continue;
		MetroCityTownState &state = GetTownState(t->index);
		if (state.match_day != 0 && state.match_day < today) {
			/* The match is over: the floodlights go back to normal. */
			state.match_day = 0;
			if (auto it = _metro_stadiums.find(t->index.base()); it != _metro_stadiums.end() && it->second != INVALID_TILE) {
				for (TileIndex tile : TileArea(it->second, 2, 2)) MarkTileDirtyByTile(tile);
			}
		}

		TileIndex stadium = FindTownStadium(t);
		_metro_stadiums[t->index.base()] = stadium;
		if (stadium == INVALID_TILE) {
			state.next_match = 0;
			continue;
		}
		if (state.next_match == 0) {
			state.next_match = today + METRO_MATCH_MIN_INTERVAL_DAYS + RandomRange(METRO_MATCH_MAX_INTERVAL_DAYS - METRO_MATCH_MIN_INTERVAL_DAYS + 1);
			state.match_level = to_underlying(RandomMatchLevel());
			state.match_announced = false;
			InvalidateWindowData(WindowClass::TownView, t->index);
			continue;
		}

		if (!state.match_announced && today + static_cast<int32_t>(METRO_MATCH_ANNOUNCE_DAYS) >= state.next_match) {
			MetroMatchLevel level = static_cast<MetroMatchLevel>(state.match_level);
			std::vector<TownID> fans = GetFanTowns(t, level);
			state.match_level = to_underlying(level);
			state.match_announced = true;
			AddTileNewsItem(GetEncodedString(STR_METRO_CITY_NEWS_MATCH_ANNOUNCE, t->index, _metro_match_specs[state.match_level].name, GetFanTownNames(fans)), NewsType::General, stadium);
			InvalidateWindowData(WindowClass::TownView, t->index);
		}

		if (today >= state.next_match) {
			MetroMatchLevel level = static_cast<MetroMatchLevel>(state.match_level);
			std::vector<TownID> fans = GetFanTowns(t, level);
			state.match_day = today;
			state.match_day_level = to_underlying(level);
			state.next_match = 0;
			AddTileNewsItem(GetEncodedString(STR_METRO_CITY_NEWS_MATCH_DAY, t->index, _metro_match_specs[state.match_day_level].name, GetFanTownNames(fans)), NewsType::General, stadium);

			/* Fans leave the stadium after the match. */
			CargoType pass = GetCargoTypeByLabel(CT_PASSENGERS);
			if (IsValidCargoType(pass)) {
				StationFinder stations(TileArea(stadium, 1, 1)); // House tiles are searched one by one.
				MoveGoodsToStation(pass, _metro_match_specs[state.match_day_level].fans, Source::Make<SourceType::Town>(t->index), stations.GetStations());
			}
			InvalidateWindowData(WindowClass::TownView, t->index);
		}
	}
}

Money MetroCityDeliveryBonus(const Station *st, CargoType cargo, uint amount, Money profit)
{
	if (amount == 0 || profit <= 0 || CargoSpec::Get(cargo)->town_acceptance_effect != TownAcceptanceEffect::Passengers) return 0;
	if (!MetroCityIsTownActive(st->town)) return 0;
	MetroCityTownState &state = GetTownState(st->town->index);
	Money bonus = 0;

	/* Tourists travel to landmarks, more of them in summer, and pay more. */
	if (IsStationNearLandmark(st)) {
		bool summer = CalTime::CurMonth() >= 5 && CalTime::CurMonth() <= 7;
		uint share = summer ? METRO_TOURIST_SHARE_SUMMER_PERCENT : METRO_TOURIST_SHARE_PERCENT;
		Money tourist_bonus = profit * share / 100 * METRO_TOURIST_EXTRA_PAY_PERCENT / 100;
		state.tourists_month += amount * share / 100;
		state.tourist_income_month += tourist_bonus;
		bonus += tourist_bonus;
	}

	/* Fans on match day. */
	if (state.match_day == CalTime::CurDate().base()) {
		auto it = _metro_stadiums.find(st->town->index.base());
		TileIndex stadium = it == _metro_stadiums.end() ? INVALID_TILE : it->second;
		if (stadium != INVALID_TILE) {
			for (TileIndex tile : TileArea(stadium, 2, 2)) {
				if (st->TileIsInCatchment(tile)) {
					bonus += profit * _metro_match_specs[state.match_day_level].bonus_percent / 100;
					break;
				}
			}
		}
	}
	return bonus;
}

/** Monthly tourist statistics. */
static void RollTouristMonth()
{
	for (auto &[town, state] : _metro_city_towns) {
		state.tourists_last_month = state.tourists_month;
		state.tourist_income_last_month = state.tourist_income_month;
		state.tourists_month = 0;
		state.tourist_income_month = 0;
		if (Town::IsValidID(town)) InvalidateWindowData(WindowClass::TownView, town);
	}
}

/** Green frame around the landmarks of active towns, when switched on. */
static void DrawLandmarkFrame(const TileInfo *ti)
{
	if (IsLandmarkTile(ti->tile) && MetroCityIsTownActive(GetTownIndex(ti->tile))) DrawTileSelectionRect(ti, SPR_ZONING_INNER_HIGHLIGHT_GREEN);
}

/** Stadium of the town whose match is played today, or INVALID_TILE. */
static TileIndex GetMatchDayStadium(TownID town, uint8_t *level = nullptr)
{
	auto it = _metro_city_towns.find(town.base());
	if (it == _metro_city_towns.end() || it->second.match_day == 0 || it->second.match_day != CalTime::CurDate().base()) return INVALID_TILE;
	auto st = _metro_stadiums.find(town.base());
	if (st == _metro_stadiums.end()) return INVALID_TILE;
	if (level != nullptr) *level = it->second.match_day_level;
	return st->second;
}

/** Current step of the floodlight flicker. */
static int GetStadiumLightBrightness()
{
	return _metro_stadium_light_brightness[(_tick_counter / METRO_STADIUM_LIGHT_FRAME_TICKS) % std::size(_metro_stadium_light_brightness)];
}

PaletteID MetroCityAdjustHousePalette(TileIndex tile, PaletteID pal)
{
	if (_metro_city_towns.empty() || !IsHouseCompleted(tile)) return pal;
	if (!HouseSpec::Get(GetHouseType(tile))->building_flags.Test(BuildingFlag::IsStadium)) return pal;
	TileIndex stadium = GetMatchDayStadium(GetTownIndex(tile));
	if (stadium == INVALID_TILE || !TileArea(stadium, 2, 2).Contains(tile)) return pal;

	int adjust = GetStadiumLightBrightness();
	if (adjust == 0) return pal;
	SetBit(pal, PALETTE_BRIGHTNESS_MODIFY);
	SB(pal, PALETTE_BRIGHTNESS_OFFSET, PALETTE_BRIGHTNESS_WIDTH, adjust & ((1 << PALETTE_BRIGHTNESS_WIDTH) - 1));
	return pal;
}

/** Vanilla effect over a random corner of the stadium (where the floodlight masts stand), @p z pixels above the ground. */
static void SpawnStadiumEffect(TileIndex stadium, EffectVehicleType type, int z)
{
	/* Cosmetic only, so the interactive random keeps the game random untouched. */
	int x = TileX(stadium) * TILE_SIZE + 2 + InteractiveRandomRange(2) * (2 * TILE_SIZE - 4);
	int y = TileY(stadium) * TILE_SIZE + 2 + InteractiveRandomRange(2) * (2 * TILE_SIZE - 4);
	CreateEffectVehicleAbove(x, y, z, type);
}

/** Match day at the stadiums on screen: flickering floodlights, sparks and flashes; runs every tick. */
static void TickStadiumAnimation()
{
	if (_metro_stadiums.empty() || IsHeadless()) return;
	for (const auto &[town, stadium] : _metro_stadiums) {
		uint8_t level;
		if (stadium == INVALID_TILE || GetMatchDayStadium(TownID{town}, &level) != stadium || !IsTileVisible(stadium, 96)) continue;
		const MetroMatchSpec &spec = _metro_match_specs[level];

		if ((_tick_counter % METRO_STADIUM_LIGHT_FRAME_TICKS) == 0) {
			for (TileIndex tile : TileArea(stadium, 2, 2)) MarkTileDirtyByTile(tile);
		}
		if (spec.spark_interval != 0 && (_tick_counter % spec.spark_interval) == 0) SpawnStadiumEffect(stadium, EV_ELECTRIC_SPARK, METRO_STADIUM_LIGHT_HEIGHT);
		if (spec.flash_interval != 0 && (_tick_counter % spec.flash_interval) == 0) SpawnStadiumEffect(stadium, EV_EXPLOSION_SMALL, METRO_STADIUM_LIGHT_HEIGHT);
	}
}

/* ---------------------------------------------------------------------------------------------- */
/* Town window                                                                                    */
/* ---------------------------------------------------------------------------------------------- */

std::vector<std::string> MetroCityGetTownViewLines(const Town *t)
{
	std::vector<std::string> lines;
	const MetroCityTownState *state = MetroCityGetTownState(t);
	if (state == nullptr || !state->active) {
		lines.push_back(GetString(STR_METRO_CITY_TOWN_INACTIVE));
		return lines;
	}
	lines.push_back(GetString(STR_METRO_CITY_TOWN_ACTIVE, CalTime::Date{state->active_since}));
	lines.push_back(GetString(STR_METRO_CITY_TOWN_SERVICES, CountMetroServices(t, MetroServiceType::School), CountMetroServices(t, MetroServiceType::Hospital),
			CountMetroServices(t, MetroServiceType::University)));
	if (t->cache.population >= GetMetroServiceMinPopulation(MetroServiceType::School) && CountMetroServices(t, MetroServiceType::School) == 0) {
		lines.push_back(GetString(STR_METRO_CITY_TOWN_NO_SCHOOL));
	}
	if (state->tourists_last_month > 0 || state->tourist_income_last_month > 0) {
		lines.push_back(GetString(STR_METRO_CITY_TOWN_TOURISTS, state->tourists_last_month, state->tourist_income_last_month));
	}
	if (state->next_match != 0 && state->match_announced) {
		lines.push_back(GetString(STR_METRO_CITY_TOWN_MATCH, _metro_match_specs[state->match_level].name, CalTime::Date{state->next_match}));
	}
	for (std::string &line : MetroTownGetViewLines(t)) lines.push_back(std::move(line));
	return lines;
}

/* ---------------------------------------------------------------------------------------------- */
/* Console                                                                                        */
/* ---------------------------------------------------------------------------------------------- */

/** Town given by the console argument, or nullptr with an error printed. */
static Town *GetConsoleTown(std::string_view arg)
{
	auto id = ParseInteger<uint>(arg);
	Town *t = id.has_value() ? Town::GetIfValid(*id) : nullptr;
	if (t == nullptr) IConsolePrint(CC_ERROR, "Invalid town id.");
	return t;
}

/** Company that builds for console commands: the local one, or the first company when there is none (headless tests). */
static CompanyID GetConsoleCompany()
{
	if (Company::IsValidID(_local_company)) return _local_company;
	for (const Company *c : Company::Iterate()) return c->index;
	return CompanyID::Invalid();
}

/** Find a tile near the town centre where the company can build the service, or INVALID_TILE. */
static TileIndex FindServiceTile(const Town *t, MetroServiceType type, CompanyID company)
{
	Backup<CompanyID> cur_company(_current_company, company, FILE_LINE);
	TileIndex found = INVALID_TILE;
	std::map<StringID, int> errors;
	for (TileIndex tile : SpiralTileSequence(t->xy, 2 * _settings_game.economy.dist_local_authority)) {
		CommandCost ret = Command<Commands::MetroBuildService>::Do(DoCommandFlags{}, tile, to_underlying(type));
		if (ret.Succeeded()) {
			found = tile;
			break;
		}
		errors[ret.GetErrorMessage()]++;
	}
	cur_company.Restore();
	if (found == INVALID_TILE) {
		for (const auto &[error, count] : errors) IConsolePrint(CC_ERROR, "{} tiles: {}", count, GetString(error));
	}
	return found;
}

bool ConMetroCity(std::span<std::string_view> argv)
{
	if (argv.empty()) {
		IConsolePrint(CC_HELP, "Metro mod towns. Usage: metro_city towns | activate <town id> | deactivate <town id> | all | build <0-2> <town id> | services <town id> | view <town id> | match <town id> [0-2] | landmarks'.");
		IConsolePrint(CC_HELP, "  build: 0 school, 1 hospital, 2 university; built by your company near the town centre.");
		return true;
	}
	std::string_view cmd = argv.size() > 1 ? argv[1] : "towns";
	if (cmd == "towns") {
		for (const Town *t : Town::Iterate()) {
			const MetroCityTownState *state = MetroCityGetTownState(t);
			if (state == nullptr || !state->active) {
				IConsolePrint(CC_DEFAULT, "{}: {} (population {})", t->index, GetString(STR_TOWN_NAME, t->index), t->cache.population);
				continue;
			}
			IConsolePrint(CC_DEFAULT, "{}: {} (population {}), active since {}", t->index, GetString(STR_TOWN_NAME, t->index),
					t->cache.population, GetString(STR_JUST_DATE_LONG, CalTime::Date{state->active_since}));
		}
		return true;
	}
	if (cmd == "activate" && argv.size() > 2) {
		Town *t = GetConsoleTown(argv[2]);
		if (t == nullptr) return true;
		ActivateTown(t);
		IConsolePrint(CC_INFO, "{} is active.", GetString(STR_TOWN_NAME, t->index));
		return true;
	}
	if (cmd == "deactivate" && argv.size() > 2) {
		/* For tests: the town becomes active again with the next transport at a station in it. */
		for (Town *t : Town::Iterate()) {
			if (argv[2] != "all" && ParseInteger<uint>(argv[2]) != t->index.base()) continue;
			auto it = _metro_city_towns.find(t->index.base());
			if (it == _metro_city_towns.end() || !it->second.active) continue;
			it->second.active = false;
			InvalidateWindowData(WindowClass::TownView, t->index);
			IConsolePrint(CC_INFO, "{} is not active.", GetString(STR_TOWN_NAME, t->index));
		}
		return true;
	}
	if (cmd == "build" && argv.size() > 3) {
		auto type = ParseInteger<uint>(argv[2]);
		Town *t = GetConsoleTown(argv[3]);
		if (t == nullptr) return true;
		CompanyID company = GetConsoleCompany();
		if (!type.has_value() || *type >= METRO_SERVICE_COUNT || company == CompanyID::Invalid()) {
			IConsolePrint(CC_ERROR, "Invalid service type or no company.");
			return true;
		}
		TileIndex tile = FindServiceTile(t, static_cast<MetroServiceType>(*type), company);
		if (tile == INVALID_TILE) {
			IConsolePrint(CC_ERROR, "No suitable tile found.");
			return true;
		}
		/* Console scripts may run from a timer, where the current company is not the local one. */
		Backup<CompanyID> cur_company(_current_company, company, FILE_LINE);
		if (company == _local_company) {
			Command<Commands::MetroBuildService>::Post(STR_METRO_CITY_ERR_CANT_BUILD, tile, static_cast<uint8_t>(*type));
		} else {
			Command<Commands::MetroBuildService>::Do(DoCommandFlag::Execute, tile, static_cast<uint8_t>(*type));
		}
		cur_company.Restore();
		IConsolePrint(CC_INFO, "Built at tile {} ({}, {}).", tile, TileX(tile), TileY(tile));
		return true;
	}
	if (cmd == "match" && argv.size() > 2) {
		Town *t = GetConsoleTown(argv[2]);
		if (t == nullptr) return true;
		if (!MetroCityIsTownActive(t) || FindTownStadium(t) == INVALID_TILE) {
			IConsolePrint(CC_ERROR, "The town is not active or has no stadium.");
			return true;
		}
		MetroCityTownState &state = GetTownState(t->index);
		state.next_match = CalTime::CurDate().base() + METRO_MATCH_ANNOUNCE_DAYS;
		if (argv.size() > 3) state.match_level = static_cast<uint8_t>(std::min<uint>(ParseInteger<uint>(argv[3]).value_or(0), to_underlying(MetroMatchLevel::Final)));
		state.match_announced = false;
		IConsolePrint(CC_INFO, "Match in {} days; the news comes tomorrow.", METRO_MATCH_ANNOUNCE_DAYS);
		return true;
	}
	if (cmd == "landmarks") {
		MetroCityToggleLandmarks();
		SetWindowClassesDirty(WindowClass::TownView);
		return true;
	}
	if ((cmd == "services" || cmd == "view") && argv.size() > 2) {
		Town *t = GetConsoleTown(argv[2]);
		if (t == nullptr) return true;
		std::vector<TileIndex> tiles;
		for (const Object *o : Object::Iterate()) {
			if (IsMetroServiceObject(o->type) && o->town == t) tiles.push_back(o->location.tile);
		}
		for (TileIndex tile : tiles) {
			MetroServiceType type;
			TownID town;
			int32_t build_date;
			MetroCityGetServiceInfo(tile, type, town, build_date);
			IConsolePrint(CC_DEFAULT, "{} at {} ({}, {})", GetString(GetMetroServiceName(type)), tile, TileX(tile), TileY(tile));
			if (cmd == "view") ShowMetroServiceWindow(tile);
		}
		if (cmd == "view" && !tiles.empty()) ScrollMainWindowToTile(tiles.front(), true);
		return true;
	}
	IConsolePrint(CC_ERROR, "Unknown subcommand.");
	return true;
}
