/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_city.cpp Metro mod: active towns, town services, fires, tourists and stadium matches.
 *
 * A town becomes active for good when the first station of the player appears in its local authority zone.
 * Everything of this file (services, fires, tourists, matches) and the AI rating work only in active towns.
 */

#include "stdafx.h"
#include "metro_city.h"
#include "metro_city_cmd.h"
#include "bridge_map.h"
#include "cargotype.h"
#include "clear_map.h"
#include "date_func.h"
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
#include "tunnelbridge.h"
#include "tunnelbridge_map.h"
#include "vehicle_base.h"
#include "viewport_func.h"
#include "window_func.h"
#include "core/backup_type.hpp"
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
std::vector<MetroFirePost> _metro_fire_posts;
std::vector<MetroFire> _metro_fires;
std::vector<MetroFireTruck> _metro_fire_trucks;
bool _metro_show_landmarks = false;

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

/** Activate the towns that got their first station of a human player. Activation is permanent. */
static void UpdateActiveTowns()
{
	for (const Station *st : Station::Iterate()) {
		if (!Company::IsValidHumanID(st->owner)) continue;
		Town *t = GetLocalAuthorityTown(st->xy);
		if (t == nullptr || MetroCityIsTownActive(t)) continue;

		MetroCityTownState &state = GetTownState(t->index);
		state.active = true;
		state.active_since = CalTime::CurDate().base();
		InvalidateWindowData(WindowClass::TownView, t->index);
		SetWindowDirty(WindowClass::TownAuthority, t->index);
	}
}

void MetroCityReset()
{
	_metro_city_towns.clear();
	_metro_fire_posts.clear();
	_metro_fires.clear();
	_metro_fire_trucks.clear();
}

void MetroCityOnTownDeleted(TownID town)
{
	_metro_city_towns.erase(town.base());
}

static void ValidateFirePosts();
static void MaybeStartFires();
static void TickFires();
static void DrawFireTrucks(const TileInfo *ti);

static const IntervalTimer<TimerGameCalendar> _metro_city_daily({TimerGameCalendar::Trigger::Day, TimerGameCalendar::Priority::None}, [](auto) {
	UpdateActiveTowns();
	ValidateFirePosts();
	MaybeStartFires();
});

/* ---------------------------------------------------------------------------------------------- */
/* Services                                                                                       */
/* ---------------------------------------------------------------------------------------------- */

static_assert(OBJECT_METRO_HOSPITAL == OBJECT_METRO_SCHOOL + to_underlying(MetroServiceType::Hospital));
static_assert(OBJECT_METRO_UNIVERSITY == OBJECT_METRO_SCHOOL + to_underlying(MetroServiceType::University));
static_assert(OBJECT_METRO_FIRE_STATION == OBJECT_METRO_SCHOOL + to_underlying(MetroServiceType::FireStation));

static const MetroServiceSpec &GetServiceSpec(MetroServiceType type)
{
	return _metro_service_specs[to_underlying(type)];
}

bool IsMetroServiceObject(ObjectType type)
{
	return type >= OBJECT_METRO_SCHOOL && type < OBJECT_METRO_SCHOOL + METRO_OBJECT_COUNT;
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

const MetroFirePost *GetMetroFirePost(TileIndex tile)
{
	for (const MetroFirePost &post : _metro_fire_posts) {
		if (post.tile == tile) return &post;
	}
	return nullptr;
}

uint CountMetroServices(const Town *t, MetroServiceType type)
{
	uint count = 0;
	if (type == MetroServiceType::FirePost) {
		for (const MetroFirePost &post : _metro_fire_posts) {
			if (post.town == t->index.base()) count++;
		}
		return count;
	}
	ObjectType object = GetServiceSpec(type).object;
	for (const Object *o : Object::Iterate()) {
		if (o->type == object && o->town == t) count++;
	}
	return count;
}

bool MetroCityGetServiceInfo(TileIndex tile, MetroServiceType &type, TownID &town, int32_t &build_date)
{
	if (const MetroFirePost *post = GetMetroFirePost(tile); post != nullptr) {
		type = MetroServiceType::FirePost;
		town = TownID{post->town};
		build_date = post->build_date;
		return true;
	}
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

/** Fire engines of a fire station or post that are not out. */
static uint CountTrucksAtHome(TileIndex home, MetroServiceType type)
{
	uint out = 0;
	for (const MetroFireTruck &truck : _metro_fire_trucks) {
		if (truck.home == home) out++;
	}
	uint total = GetServiceSpec(type).trucks;
	return total > out ? total - out : 0;
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
	if (spec.trucks > 0) {
		lines.push_back(GetString(STR_METRO_SERVICE_TRUCKS, CountTrucksAtHome(tile, type), spec.trucks));
		lines.push_back(GetString(STR_METRO_SERVICE_RADIUS, spec.radius));
	}
	if (type == MetroServiceType::Hospital && !HasRoadNextTo(tile)) lines.push_back(GetString(STR_METRO_SERVICE_NO_ROAD));
	return lines;
}

extern CommandCost CheckBuildableTile(TileIndex tile, DiagDirections invalid_dirs, int &allowed_z, bool allow_steep, bool check_bridge);

/** Whether the tile is a flat straight road where a fire post can stand. */
static bool IsStraightRoadForPost(TileIndex tile)
{
	if (!IsNormalRoadTile(tile) || GetTileSlope(tile) != SLOPE_FLAT) return false;
	RoadBits bits = GetAllRoadBits(tile);
	return bits == ROAD_X || bits == ROAD_Y;
}

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

	if (type == MetroServiceType::FirePost) {
		if (GetMetroFirePost(tile) != nullptr) return CommandCost(STR_METRO_CITY_ERR_POST_EXISTS);
		if (!IsStraightRoadForPost(tile)) return CommandCost(STR_METRO_CITY_ERR_STRAIGHT_ROAD);
		Owner road_owner = GetRoadOwner(tile, RoadTramType::Road);
		if (road_owner != OWNER_TOWN && road_owner != OWNER_NONE && road_owner != _current_company) return CommandCost(STR_ERROR_OWNED_BY);

		if (flags.Test(DoCommandFlag::Execute)) {
			_metro_fire_posts.push_back({tile, t->index.base(), _current_company.base(), CalTime::CurDate().base()});
			MarkTileDirtyByTile(tile);
			InvalidateWindowData(WindowClass::TownView, t->index);
			InvalidateWindowData(WindowClass::MetroTownServices, t->index);
		}
		return cost;
	}

	if (!Object::CanAllocateItem()) return CommandCost(STR_ERROR_TOO_MANY_OBJECTS);
	if (IsBridgeAbove(tile)) return CommandCost(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);

	/* A fire station may replace a small house of the town: a plain replacement without a rating penalty. */
	bool replace_house = false;
	if (type == MetroServiceType::FireStation && IsTileType(tile, TileType::House)) {
		const HouseSpec *hs = HouseSpec::Get(GetHouseType(tile));
		if (GetTownIndex(tile) != t->index || !hs->building_flags.Test(BuildingFlag::Size1x1) || hs->population > METRO_SMALL_HOUSE_MAX_POPULATION) {
			return CommandCost(STR_METRO_CITY_ERR_HOUSE_TOO_BIG);
		}
		replace_house = true;
	} else {
		/* Services stand on free land, not on roads of the town. */
		if (IsTileType(tile, TileType::Road)) return CommandCost(STR_ERROR_SITE_UNSUITABLE);
		CommandCost ret = Command<Commands::LandscapeClear>::Do(DoCommandFlags{flags}.Reset(DoCommandFlag::Execute), tile);
		if (ret.Failed()) return ret;
		cost.AddCost(std::move(ret));

		auto [slope, allowed_z] = GetTileSlopeZ(tile);
		if (slope != SLOPE_FLAT) allowed_z++;
		ret = CheckBuildableTile(tile, {}, allowed_z, false, false);
		if (ret.Failed()) return ret;
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		if (replace_house) {
			ClearTownHouse(t, tile);
		} else {
			Command<Commands::LandscapeClear>::Do(flags, tile);
		}
		BuildObject(spec.object, tile, _current_company, t);
		InvalidateWindowData(WindowClass::TownView, t->index);
		InvalidateWindowData(WindowClass::MetroTownServices, t->index);
	}
	return cost;
}

/**
 * Remove a fire post.
 * @param flags Type of operation.
 * @param tile Tile of the post.
 * @return The cost of this operation or an error.
 */
CommandCost CmdMetroRemoveFirePost(DoCommandFlags flags, TileIndex tile)
{
	auto it = std::ranges::find(_metro_fire_posts, tile, &MetroFirePost::tile);
	if (it == _metro_fire_posts.end()) return CMD_ERROR;
	if (_current_company != OWNER_DEITY && it->owner != _current_company.base()) return CommandCost(STR_ERROR_OWNED_BY);

	if (flags.Test(DoCommandFlag::Execute)) {
		TownID town{it->town};
		_metro_fire_posts.erase(it);
		MarkTileDirtyByTile(tile);
		InvalidateWindowData(WindowClass::TownView, town);
		InvalidateWindowData(WindowClass::MetroTownServices, town);
	}
	return CommandCost(ExpensesType::Construction);
}

/** Drop fire posts whose road is gone or no longer straight. */
static void ValidateFirePosts()
{
	for (auto it = _metro_fire_posts.begin(); it != _metro_fire_posts.end();) {
		if (IsStraightRoadForPost(it->tile) && Town::IsValidID(it->town)) {
			++it;
			continue;
		}
		MarkTileDirtyByTile(it->tile);
		if (Town::IsValidID(it->town)) InvalidateWindowData(WindowClass::TownView, it->town);
		it = _metro_fire_posts.erase(it);
	}
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
	return std::max(rate, 1u);
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

/** Draw a fire post on its road tile, like a drive-through stop. */
static void DrawFirePost(const TileInfo *ti)
{
	if (GetMetroFirePost(ti->tile) == nullptr || IsInvisibilitySet(TransparencyOption::Buildings)) return;
	bool transparent = IsTransparencySet(TransparencyOption::Buildings);
	if (GetAllRoadBits(ti->tile) == ROAD_X) {
		AddSortableSpriteToDraw(SPR_BUS_STOP_DT_X_W | (1U << PALETTE_MODIFIER_COLOUR), METRO_FIRE_POST_PALETTE, ti->x, ti->y, ti->z, SpriteBounds({0, 0, 0}, {16, 3, 16}, {}), transparent);
		AddSortableSpriteToDraw(SPR_BUS_STOP_DT_X_E | (1U << PALETTE_MODIFIER_COLOUR), METRO_FIRE_POST_PALETTE, ti->x, ti->y, ti->z, SpriteBounds({0, 13, 0}, {16, 3, 16}, {}), transparent);
	} else {
		AddSortableSpriteToDraw(SPR_BUS_STOP_DT_Y_W | (1U << PALETTE_MODIFIER_COLOUR), METRO_FIRE_POST_PALETTE, ti->x, ti->y, ti->z, SpriteBounds({13, 0, 0}, {3, 16, 16}, {}), transparent);
		AddSortableSpriteToDraw(SPR_BUS_STOP_DT_Y_E | (1U << PALETTE_MODIFIER_COLOUR), METRO_FIRE_POST_PALETTE, ti->x, ti->y, ti->z, SpriteBounds({0, 0, 0}, {3, 16, 16}, {}), transparent);
	}
}

void MetroCityDrawTile(const TileInfo *ti)
{
	if (IsTileType(ti->tile, TileType::Road) && !_metro_fire_posts.empty()) DrawFirePost(ti);
	if (!_metro_fire_trucks.empty()) DrawFireTrucks(ti);
}

void MetroCityToggleLandmarks()
{
	_metro_show_landmarks = !_metro_show_landmarks;
	MarkWholeScreenDirty();
}

static const IntervalTimer<TimerGameTick> _metro_city_ticks({TimerGameTick::Priority::None, 1}, [](auto) {
	TickFires();
});

static const IntervalTimer<TimerGameTick> _metro_city_service_ticks({TimerGameTick::Priority::None, 256}, [](auto) {
	ProduceServicePassengers();
});

static const IntervalTimer<TimerGameCalendar> _metro_city_monthly({TimerGameCalendar::Trigger::Month, TimerGameCalendar::Priority::None}, [](auto) {
	CheckHospitalAccess();
});

/* ---------------------------------------------------------------------------------------------- */
/* Fires and fire engines                                                                         */
/* ---------------------------------------------------------------------------------------------- */

/** Whether a fire engine coming from the @p side of the tile can drive onto it. */
static bool RoadOpenFrom(TileIndex tile, DiagDirection side)
{
	if (!IsValidTile(tile)) return false;
	switch (GetTileType(tile)) {
		case TileType::TunnelBridge:
			return GetTunnelBridgeTransportType(tile) == TransportType::Road && ReverseDiagDir(GetTunnelBridgeDirection(tile)) == side;
		case TileType::Road:
			if (IsRoadDepot(tile)) return false;
			[[fallthrough]];
		case TileType::Station:
			return (GetAnyRoadBits(tile, RoadTramType::Road) & DiagDirToRoadBits(side)) != ROAD_NONE;
		default:
			return false;
	}
}

/** Call @p func for every road tile a fire engine can reach from @p tile in one step. */
template <typename Func>
static void ForRoadNeighbours(TileIndex tile, Func func)
{
	if (IsTileType(tile, TileType::TunnelBridge)) {
		if (GetTunnelBridgeTransportType(tile) != TransportType::Road) return;
		DiagDirection dir = GetTunnelBridgeDirection(tile);
		func(GetOtherTunnelBridgeEnd(tile));
		TileIndex back = TileAddByDiagDir(tile, ReverseDiagDir(dir));
		if (RoadOpenFrom(back, dir)) func(back);
		return;
	}
	RoadBits bits = GetAnyRoadBits(tile, RoadTramType::Road);
	for (DiagDirection dir : EnumRange(DiagDirection::End)) {
		if ((bits & DiagDirToRoadBits(dir)) == ROAD_NONE) continue;
		TileIndex next = TileAddByDiagDir(tile, dir);
		if (RoadOpenFrom(next, ReverseDiagDir(dir))) func(next);
	}
}

/** Road tiles a fire engine of the station or post starts from. */
static std::vector<TileIndex> GetHomeRoadTiles(TileIndex home)
{
	if (GetMetroFirePost(home) != nullptr) return {home};
	std::vector<TileIndex> tiles;
	for (DiagDirection dir : EnumRange(DiagDirection::End)) {
		TileIndex adj = TileAddByDiagDir(home, dir);
		if (IsValidTile(adj) && IsTileType(adj, TileType::Road) && !IsRoadDepot(adj)) tiles.push_back(adj);
	}
	return tiles;
}

/** Shortest road route (in steps) from one of @p starts to a tile next to @p target; empty if there is none. */
static std::vector<TileIndex> FindRoadRoute(const std::vector<TileIndex> &starts, TileIndex target)
{
	std::unordered_map<uint32_t, uint32_t> parent;
	std::deque<TileIndex> queue;
	for (TileIndex tile : starts) {
		if (parent.emplace(tile.base(), tile.base()).second) queue.push_back(tile);
	}
	while (!queue.empty() && parent.size() < METRO_TRUCK_MAX_SEARCH) {
		TileIndex tile = queue.front();
		queue.pop_front();
		if (DistanceManhattan(tile, target) == 1 || tile == target) {
			std::vector<TileIndex> route;
			for (uint32_t cur = tile.base();; cur = parent[cur]) {
				route.push_back(TileIndex{cur});
				if (parent[cur] == cur) break;
			}
			std::reverse(route.begin(), route.end());
			return route;
		}
		ForRoadNeighbours(tile, [&](TileIndex next) {
			if (parent.emplace(next.base(), tile.base()).second) queue.push_back(next);
		});
	}
	return {};
}

/** Route back home from a road tile. */
static std::vector<TileIndex> FindRouteHome(TileIndex from, TileIndex home)
{
	return FindRoadRoute({from}, home);
}

static MetroFire *GetFire(TileIndex tile)
{
	for (MetroFire &fire : _metro_fires) {
		if (fire.tile == tile) return &fire;
	}
	return nullptr;
}

/** Type of the fire service at @p home, or #MetroServiceType::End when it is gone. */
static MetroServiceType GetFireHomeType(TileIndex home)
{
	if (GetMetroFirePost(home) != nullptr) return MetroServiceType::FirePost;
	const Object *o = GetServiceObject(home);
	if (o != nullptr && o->type == OBJECT_METRO_FIRE_STATION) return MetroServiceType::FireStation;
	return MetroServiceType::End;
}

/** Send the nearest free fire engine that can reach the fire by road. @return Whether one is on its way. */
static bool DispatchFireTruck(const MetroFire &fire)
{
	std::vector<std::pair<uint, TileIndex>> homes;
	auto consider = [&](TileIndex home, MetroServiceType type) {
		if (!MetroCityIsTownActive(TownID{type == MetroServiceType::FirePost ? GetMetroFirePost(home)->town : Object::GetByTile(home)->town->index.base()})) return;
		uint distance = DistanceManhattan(home, fire.tile);
		if (distance <= GetServiceSpec(type).radius && CountTrucksAtHome(home, type) > 0) homes.emplace_back(distance, home);
	};
	for (const Object *o : Object::Iterate()) {
		if (o->type == OBJECT_METRO_FIRE_STATION) consider(o->location.tile, MetroServiceType::FireStation);
	}
	for (const MetroFirePost &post : _metro_fire_posts) consider(post.tile, MetroServiceType::FirePost);
	std::sort(homes.begin(), homes.end());

	for (const auto &[distance, home] : homes) {
		std::vector<TileIndex> route = FindRoadRoute(GetHomeRoadTiles(home), fire.tile);
		if (route.empty()) continue;
		MetroFireTruck &truck = _metro_fire_trucks.emplace_back();
		truck.home = home;
		truck.fire = fire.tile;
		truck.pos = route.front();
		truck.state = to_underlying(MetroTruckState::ToFire);
		truck.path = std::move(route);
		MarkTileDirtyByTile(truck.pos);
		return true;
	}
	return false;
}

/** Start a fire in a random completed house of the town. @return Whether a house caught fire. */
static bool StartFire(Town *t)
{
	uint radius = std::max<uint>(2, IntSqrt(t->cache.squared_town_zone_radius[to_underlying(HouseZone::TownEdge)]));
	for (int attempt = 0; attempt < 40; attempt++) {
		int dx = RandomRange(2 * radius + 1) - radius;
		int dy = RandomRange(2 * radius + 1) - radius;
		TileIndex tile = TileAddWrap(t->xy, dx, dy);
		if (tile == INVALID_TILE || !IsTileType(tile, TileType::House) || GetTownIndex(tile) != t->index || !IsHouseCompleted(tile)) continue;
		if (GetFire(tile) != nullptr) continue;

		MetroFire &fire = _metro_fires.emplace_back();
		fire.tile = tile;
		fire.town = t->index.base();
		fire.ticks_left = METRO_FIRE_TIMER_TICKS;
		bool dispatched = DispatchFireTruck(fire);
		AddTileNewsItem(GetEncodedString(dispatched ? STR_METRO_CITY_NEWS_FIRE : STR_METRO_CITY_NEWS_FIRE_NO_ENGINE, t->index), NewsType::Accident, tile);
		InvalidateWindowData(WindowClass::TownView, t->index);
		return true;
	}
	return false;
}

/** Daily chance of a fire in every active town without a fire. */
static void MaybeStartFires()
{
	for (Town *t : Town::Iterate()) {
		if (!MetroCityIsTownActive(t) || !Chance16(1, METRO_FIRE_DAILY_CHANCE)) continue;
		if (std::ranges::any_of(_metro_fires, [&](const MetroFire &fire) { return fire.town == t->index.base(); })) continue;
		StartFire(t);
	}
}

/** The fire is over: put out by the firefighters or the house burned down. */
static void EndFire(TileIndex tile, bool put_out)
{
	auto it = std::ranges::find(_metro_fires, tile, &MetroFire::tile);
	if (it == _metro_fires.end()) return;
	TownID town{it->town};
	_metro_fires.erase(it);

	if (Town::IsValidID(town)) {
		MetroCityTownState &state = GetTownState(town);
		if (put_out) {
			state.fires_put_out++;
			AddTileNewsItem(GetEncodedString(STR_METRO_CITY_NEWS_FIRE_OUT, town), NewsType::Accident, tile);
		} else {
			state.houses_burned++;
			if (IsTileType(tile, TileType::House)) {
				ClearTownHouse(Town::Get(town), tile);
				if (IsTileType(tile, TileType::Clear)) MakeClear(tile, ClearGround::Rough, 3);
				MarkTileDirtyByTile(tile);
			}
			AddTileNewsItem(GetEncodedString(STR_METRO_CITY_NEWS_BURNED, town), NewsType::Accident, tile);
		}
		InvalidateWindowData(WindowClass::TownView, town);
	}

	/* Engines on their way turn back. */
	for (MetroFireTruck &truck : _metro_fire_trucks) {
		if (truck.fire != tile || truck.state == to_underlying(MetroTruckState::Returning)) continue;
		truck.state = to_underlying(MetroTruckState::Returning);
		truck.path.clear();
		truck.progress = 0;
	}
}

/** Whether a vehicle blocks the road tile (traffic jam for the fire engine). */
static bool IsRoadBlocked(TileIndex tile)
{
	auto blocks = [](const Vehicle *v) { return !v->vehstatus.Test(VehState::Hidden); };
	return HasVehicleOnTile(tile, VehicleType::Road, blocks) || HasVehicleOnTile(tile, VehicleType::Train, blocks);
}

/** Move one fire engine by one tick. @return False when the engine is back home (or lost) and should be removed. */
static bool TickFireTruck(MetroFireTruck &truck)
{
	MetroTruckState state = static_cast<MetroTruckState>(truck.state);
	if (GetFireHomeType(truck.home) == MetroServiceType::End) return false; // The station is gone.

	if (state == MetroTruckState::Extinguishing) {
		if (GetFire(truck.fire) == nullptr) {
			truck.state = to_underlying(MetroTruckState::Returning);
			return true;
		}
		if (++truck.timer >= METRO_FIRE_EXTINGUISH_TICKS) {
			EndFire(truck.fire, true);
			truck.state = to_underlying(MetroTruckState::Returning);
			truck.path.clear();
		}
		return true;
	}

	if (truck.path.empty()) {
		/* After loading or turning back: find the route again. */
		truck.path = (state == MetroTruckState::ToFire) ? FindRoadRoute({truck.pos}, truck.fire) : FindRouteHome(truck.pos, truck.home);
		truck.progress = 0;
		if (truck.path.empty()) {
			if (state == MetroTruckState::ToFire) {
				truck.state = to_underlying(MetroTruckState::Returning);
				return true;
			}
			MarkTileDirtyByTile(truck.pos);
			return false;
		}
	}

	if (truck.path.size() == 1) {
		MarkTileDirtyByTile(truck.pos);
		if (state == MetroTruckState::Returning) return false;
		truck.state = to_underlying(MetroTruckState::Extinguishing);
		truck.timer = 0;
		if (MetroFire *fire = GetFire(truck.fire); fire != nullptr) fire->controlled = true;
		return true;
	}

	TileIndex next = truck.path[1];
	if (truck.progress == 0) {
		/* Wait at the tile border while the next tile is occupied: fire engines get stuck in traffic jams too. */
		if (IsRoadBlocked(next)) return true;
		truck.step_ticks = static_cast<uint16_t>(METRO_TRUCK_TICKS_PER_TILE * DistanceManhattan(truck.pos, next));
	}
	truck.progress++;
	MarkTileDirtyByTile(truck.pos);
	MarkTileDirtyByTile(next);
	if (truck.progress >= truck.step_ticks) {
		truck.pos = next;
		truck.path.erase(truck.path.begin());
		truck.progress = 0;
	}
	return true;
}

/** Fire timers, smoke and fire engines; runs every tick. */
static void TickFires()
{
	if (_metro_fires.empty() && _metro_fire_trucks.empty()) return;

	for (auto it = _metro_fire_trucks.begin(); it != _metro_fire_trucks.end();) {
		if (TickFireTruck(*it)) {
			++it;
		} else {
			if (it->home != INVALID_TILE) SetWindowDirty(WindowClass::MetroService, it->home.base());
			it = _metro_fire_trucks.erase(it);
		}
	}

	std::vector<TileIndex> burned;
	for (MetroFire &fire : _metro_fires) {
		if (!IsTileType(fire.tile, TileType::House) || !Town::IsValidID(fire.town)) {
			burned.push_back(fire.tile);
			continue;
		}
		if ((_tick_counter % METRO_FIRE_SMOKE_INTERVAL) == 0) {
			CreateEffectVehicleAbove(TileX(fire.tile) * TILE_SIZE + TILE_SIZE / 2, TileY(fire.tile) * TILE_SIZE + TILE_SIZE / 2, 12, EV_BREAKDOWN_SMOKE);
		}
		if ((_tick_counter % DAY_TICKS) == 0) SetWindowDirty(WindowClass::TownView, fire.town);
		if (fire.controlled) continue;
		if (fire.ticks_left > 0) fire.ticks_left--;
		if (fire.ticks_left == 0) burned.push_back(fire.tile);
	}
	for (TileIndex tile : burned) EndFire(tile, false);
}

/** Draw the fire engines that are on the tile. */
static void DrawFireTrucks(const TileInfo *ti)
{
	for (const MetroFireTruck &truck : _metro_fire_trucks) {
		if (truck.pos != ti->tile) continue;
		TileIndex next = truck.path.size() > 1 ? truck.path[1] : truck.pos;
		if (DistanceManhattan(truck.pos, next) > 1 && truck.progress > 0) continue; // Inside a tunnel or on a long bridge jump.

		int x = TileX(truck.pos) * TILE_SIZE + TILE_SIZE / 2;
		int y = TileY(truck.pos) * TILE_SIZE + TILE_SIZE / 2;
		Direction dir = Direction::SW;
		if (next != truck.pos) {
			dir = DiagDirToDir(DiagdirBetweenTiles(truck.pos, next));
			if (truck.step_ticks > 0) {
				x += (static_cast<int>(TileX(next)) - static_cast<int>(TileX(truck.pos))) * static_cast<int>(TILE_SIZE) * truck.progress / truck.step_ticks;
				y += (static_cast<int>(TileY(next)) - static_cast<int>(TileY(truck.pos))) * static_cast<int>(TILE_SIZE) * truck.progress / truck.step_ticks;
			}
		}
		int z = GetSlopePixelZ(x, y, true);
		AddSortableSpriteToDraw(METRO_TRUCK_SPRITE + to_underlying(dir), METRO_TRUCK_PALETTE, x, y, z, SpriteBounds({-3, -3, 0}, {6, 6, 6}, {3, 3, 0}));
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
			CountMetroServices(t, MetroServiceType::University), CountMetroServices(t, MetroServiceType::FireStation), CountMetroServices(t, MetroServiceType::FirePost)));
	if (t->cache.population >= GetMetroServiceMinPopulation(MetroServiceType::School) && CountMetroServices(t, MetroServiceType::School) == 0) {
		lines.push_back(GetString(STR_METRO_CITY_TOWN_NO_SCHOOL));
	}
	for (const MetroFire &fire : _metro_fires) {
		if (fire.town == t->index.base()) lines.push_back(GetString(STR_METRO_CITY_TOWN_FIRE_NOW, fire.ticks_left * 30 / 1000));
	}
	if (state->fires_put_out > 0 || state->houses_burned > 0) lines.push_back(GetString(STR_METRO_CITY_TOWN_FIRES, state->fires_put_out, state->houses_burned));
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
		IConsolePrint(CC_HELP, "Metro mod towns. Usage: 'metro_city towns | activate <town id> | build <0-4> <town id> | services <town id> | view <town id> | fire <town id>'.");
		IConsolePrint(CC_HELP, "  build: 0 school, 1 hospital, 2 university, 3 fire station, 4 fire post; built by your company near the town centre.");
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
			IConsolePrint(CC_DEFAULT, "{}: {} (population {}), active; fires put out {}, houses burned {}", t->index, GetString(STR_TOWN_NAME, t->index),
					t->cache.population, state->fires_put_out, state->houses_burned);
		}
		return true;
	}
	if (cmd == "activate" && argv.size() > 2) {
		Town *t = GetConsoleTown(argv[2]);
		if (t == nullptr) return true;
		MetroCityTownState &state = GetTownState(t->index);
		if (!state.active) {
			state.active = true;
			state.active_since = CalTime::CurDate().base();
			InvalidateWindowData(WindowClass::TownView, t->index);
		}
		IConsolePrint(CC_INFO, "{} is active.", GetString(STR_TOWN_NAME, t->index));
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
	if (cmd == "fire" && argv.size() > 2) {
		Town *t = GetConsoleTown(argv[2]);
		if (t == nullptr) return true;
		if (!MetroCityIsTownActive(t)) {
			IConsolePrint(CC_ERROR, "The town is not active.");
			return true;
		}
		IConsolePrint(CC_INFO, StartFire(t) ? "A house is on fire." : "No house found.");
		for (const MetroFireTruck &truck : _metro_fire_trucks) {
			IConsolePrint(CC_DEFAULT, "Engine from {} to {}: {} road tiles", truck.home, truck.fire, truck.path.size());
		}
		return true;
	}
	if ((cmd == "services" || cmd == "view") && argv.size() > 2) {
		Town *t = GetConsoleTown(argv[2]);
		if (t == nullptr) return true;
		std::vector<TileIndex> tiles;
		for (const Object *o : Object::Iterate()) {
			if (IsMetroServiceObject(o->type) && o->town == t) tiles.push_back(o->location.tile);
		}
		for (const MetroFirePost &post : _metro_fire_posts) {
			if (post.town == t->index.base()) tiles.push_back(post.tile);
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
