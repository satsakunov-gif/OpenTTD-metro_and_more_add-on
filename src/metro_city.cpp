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
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "console_func.h"
#include "console_type.h"
#include "economy_func.h"
#include "gfx_func.h"
#include "window_gui.h"
#include "landscape_cmd.h"
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
#include "viewport_func.h"
#include "window_func.h"
#include "core/backup_type.hpp"
#include "core/string_consumer.hpp"
#include "timer/timer.h"
#include "timer/timer_game_calendar.h"
#include "timer/timer_game_tick.h"
#include "table/metro_city_tables.h"
#include "table/strings.h"

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

static const IntervalTimer<TimerGameCalendar> _metro_city_daily({TimerGameCalendar::Trigger::Day, TimerGameCalendar::Priority::None}, [](auto) {
	UpdateActiveTowns();
	ValidateFirePosts();
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
}

void MetroCityToggleLandmarks()
{
	_metro_show_landmarks = !_metro_show_landmarks;
	MarkWholeScreenDirty();
}

static const IntervalTimer<TimerGameTick> _metro_city_service_ticks({TimerGameTick::Priority::None, 256}, [](auto) {
	ProduceServicePassengers();
});

static const IntervalTimer<TimerGameCalendar> _metro_city_monthly({TimerGameCalendar::Trigger::Month, TimerGameCalendar::Priority::None}, [](auto) {
	CheckHospitalAccess();
});

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
		IConsolePrint(CC_HELP, "Metro mod towns. Usage: 'metro_city towns | activate <town id> | build <0-4> <town id> | services <town id> | view <town id>'.");
		IConsolePrint(CC_HELP, "  build: 0 school, 1 hospital, 2 university, 3 fire station, 4 fire post; built by your company near the town centre.");
		return true;
	}
	std::string_view cmd = argv.size() > 1 ? argv[1] : "towns";
	if (cmd == "towns") {
		for (const Town *t : Town::Iterate()) {
			IConsolePrint(CC_DEFAULT, "{}: {} (population {}){}", t->index, GetString(STR_TOWN_NAME, t->index), t->cache.population,
					MetroCityIsTownActive(t) ? ", active" : "");
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
