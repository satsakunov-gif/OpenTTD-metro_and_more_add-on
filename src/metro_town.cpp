/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_town.cpp Metro mod: town life of active towns.
 *
 * Districts: a town gets a business and a residential district when it becomes active, and rarely one more district
 * per #METRO_DISTRICT_POPULATION_STEP residents. A house belongs to the district with the nearest centre. Business
 * buildings and high-rises grow more often in business districts and near metro stations.
 * Commuting: workers of the residential districts travel to the jobs of the business districts and back every day
 * (no rush hour); when the town has too few jobs, the rest travel to work in a neighbour town.
 * Migration: every 5 months the attractiveness of each active town is computed; people move to a better neighbour
 * active town. The population of the worse town does not fall, it only grows slower; the movers are passengers.
 */

#include "stdafx.h"
#include "metro_town.h"
#include "metro_city.h"
#include "cargotype.h"
#include "company_base.h"
#include "console_func.h"
#include "console_type.h"
#include "date_func.h"
#include "economy_func.h"
#include "house.h"
#include "station_base.h"
#include "station_container.h"
#include "strings_func.h"
#include "tile_map.h"
#include "town.h"
#include "town_map.h"
#include "window_func.h"
#include "core/random_func.hpp"
#include "core/string_consumer.hpp"
#include "timer/timer.h"
#include "timer/timer_game_calendar.h"
#include "table/metro_town_tables.h"
#include "table/strings.h"

#include <algorithm>
#include <tuple>

#include "safeguards.h"

std::vector<MetroDistrict> _metro_districts;
std::map<uint16_t, MetroTownLifeState> _metro_town_life;

static MetroTownLifeState &GetLifeState(TownID town)
{
	auto [it, inserted] = _metro_town_life.try_emplace(town.base());
	if (inserted) it->second.town = town.base();
	return it->second;
}

void MetroTownReset()
{
	_metro_districts.clear();
	_metro_town_life.clear();
}

void MetroTownOnTownDeleted(TownID town)
{
	std::erase_if(_metro_districts, [town](const MetroDistrict &d) { return d.town == town.base(); });
	_metro_town_life.erase(town.base());
}

/* ---------------------------------------------------------------------------------------------- */
/* Districts                                                                                      */
/* ---------------------------------------------------------------------------------------------- */

uint MetroTownGetMapRadius(const Town *t)
{
	return std::max<uint>(METRO_DISTRICT_MAP_MIN_RADIUS, IntSqrt(t->cache.squared_town_zone_radius[to_underlying(HouseZone::TownEdge)]) + 2);
}

/** Call @p func for every house tile of the town. */
template <typename Func>
static void ForTownHouseTiles(const Town *t, Func func)
{
	uint radius = MetroTownGetMapRadius(t);
	for (TileIndex tile : SpiralTileSequence(t->xy, 2 * radius + 1)) {
		if (IsTileType(tile, TileType::House) && GetTownIndex(tile) == t->index) func(tile);
	}
}

static std::vector<const MetroDistrict *> GetTownDistricts(const Town *t)
{
	std::vector<const MetroDistrict *> districts;
	for (const MetroDistrict &d : _metro_districts) {
		if (d.town == t->index.base()) districts.push_back(&d);
	}
	return districts;
}

int GetMetroDistrictTypeAt(const Town *t, TileIndex tile)
{
	if (_metro_districts.empty()) return -1;
	int type = -1;
	uint best = UINT_MAX;
	for (const MetroDistrict &d : _metro_districts) {
		if (d.town != t->index.base()) continue;
		uint dist = DistanceManhattan(TileIndex{d.tile}, tile);
		if (dist < best) {
			best = dist;
			type = d.type;
		}
	}
	return type;
}

/** Population of the house tile (the main tile of a multi-tile house carries it). */
static uint32_t GetHousePopulation(TileIndex tile)
{
	return HouseSpec::Get(GetHouseType(tile))->population;
}

/** A random house tile of the town at least #METRO_DISTRICT_MIN_DISTANCE from all district centres, or INVALID_TILE. */
static TileIndex PickDistrictCentre(const Town *t)
{
	std::vector<TileIndex> houses;
	ForTownHouseTiles(t, [&](TileIndex tile) { houses.push_back(tile); });
	std::vector<const MetroDistrict *> districts = GetTownDistricts(t);
	while (!houses.empty()) {
		size_t i = RandomRange(static_cast<uint32_t>(houses.size()));
		TileIndex tile = houses[i];
		bool ok = std::ranges::none_of(districts, [&](const MetroDistrict *d) { return DistanceManhattan(TileIndex{d->tile}, tile) < METRO_DISTRICT_MIN_DISTANCE; });
		if (ok) return tile;
		houses[i] = houses.back();
		houses.pop_back();
	}
	return INVALID_TILE;
}

static void AddDistrict(const Town *t, TileIndex centre, MetroDistrictType type)
{
	_metro_districts.push_back({t->index.base(), centre.base(), to_underlying(type)});
	InvalidateWindowData(WindowClass::TownView, t->index);
	InvalidateWindowData(WindowClass::MetroTownDistricts, t->index);
}

/** Monthly: an active town gets its first two districts at once, and later rarely one more as it grows. */
static void UpdateDistricts()
{
	for (const Town *t : Town::Iterate()) {
		if (!MetroCityIsTownActive(t)) continue;
		std::vector<const MetroDistrict *> districts = GetTownDistricts(t);
		if (districts.empty()) {
			/* The first pair: one of each type, placed at random. */
			MetroDistrictType first = RandomRange(2) == 0 ? MetroDistrictType::Business : MetroDistrictType::Residential;
			MetroDistrictType second = first == MetroDistrictType::Business ? MetroDistrictType::Residential : MetroDistrictType::Business;
			TileIndex a = PickDistrictCentre(t);
			if (a == INVALID_TILE) a = t->xy;
			AddDistrict(t, a, first);
			TileIndex b = PickDistrictCentre(t);
			if (b != INVALID_TILE) AddDistrict(t, b, second);
			continue;
		}
		uint wanted = std::min<uint>(METRO_DISTRICT_MAX, 2 + t->cache.population / METRO_DISTRICT_POPULATION_STEP);
		if (districts.size() >= wanted || RandomRange(METRO_DISTRICT_NEW_CHANCE) != 0) continue;
		TileIndex centre = PickDistrictCentre(t);
		if (centre != INVALID_TILE) AddDistrict(t, centre, RandomRange(2) == 0 ? MetroDistrictType::Business : MetroDistrictType::Residential);
	}
}

std::vector<MetroDistrictStats> MetroTownGetDistrictStats(const Town *t)
{
	std::vector<MetroDistrictStats> stats(to_underlying(MetroDistrictType::End));
	for (const MetroDistrict *d : GetTownDistricts(t)) stats[d->type].districts++;
	if (GetTownDistricts(t).empty()) return stats;
	ForTownHouseTiles(t, [&](TileIndex tile) {
		int type = GetMetroDistrictTypeAt(t, tile);
		if (type < 0) return;
		stats[type].population += GetHousePopulation(tile);
		stats[type].houses++;
	});
	return stats;
}

uint32_t MetroTownGetWorkers(const Town *t)
{
	return MetroTownGetDistrictStats(t)[to_underlying(MetroDistrictType::Residential)].population * METRO_COMMUTE_WORKER_PERCENT / 100;
}

uint32_t MetroTownGetJobs(const Town *t)
{
	return MetroTownGetDistrictStats(t)[to_underlying(MetroDistrictType::Business)].population * METRO_COMMUTE_JOB_PERCENT / 100;
}

/** Whether a metro station is near the tile; until the metro exists (stage 8): a rail station of a human player. */
static bool IsNearMetroStation(TileIndex tile)
{
	for (const Station *st : Station::Iterate()) {
		if (!Company::IsValidHumanID(st->owner) || !st->facilities.Test(StationFacility::Train)) continue;
		if (DistanceMax(st->xy, tile) <= METRO_BUSINESS_STATION_RADIUS) return true;
	}
	return false;
}

uint MetroTownAdjustHouseProbability(const Town *t, TileIndex tile, const HouseSpec &hs, uint probability)
{
	if (_metro_districts.empty() || hs.population < METRO_BUSINESS_HOUSE_POPULATION || !MetroCityIsTownActive(t)) return probability;
	int type = GetMetroDistrictTypeAt(t, tile);
	if (type < 0) return probability;
	probability = probability * (type == to_underlying(MetroDistrictType::Business) ? METRO_BUSINESS_IN_BUSINESS_PERCENT : METRO_BUSINESS_IN_RESIDENTIAL_PERCENT) / 100;
	if (IsNearMetroStation(tile)) probability = probability * METRO_BUSINESS_NEAR_STATION_PERCENT / 100;
	return probability;
}

/* ---------------------------------------------------------------------------------------------- */
/* Commuting                                                                                      */
/* ---------------------------------------------------------------------------------------------- */

/** Send @p amount passengers from random houses of the district type to the stations around them; returns those that left. */
static uint SendFromDistrict(const Town *t, MetroDistrictType type, uint amount)
{
	CargoType pass = GetCargoTypeByLabel(CT_PASSENGERS);
	if (amount == 0 || !IsValidCargoType(pass)) return 0;
	std::vector<TileIndex> houses;
	ForTownHouseTiles(t, [&](TileIndex tile) {
		if (GetMetroDistrictTypeAt(t, tile) == to_underlying(type)) houses.push_back(tile);
	});
	if (houses.empty()) return 0;
	uint moved = 0;
	uint parts = std::min<uint>(METRO_COMMUTE_SPREAD_HOUSES, amount);
	for (uint i = 0; i < parts; i++) {
		uint part = amount / parts + (i < amount % parts ? 1 : 0);
		TileIndex tile = houses[RandomRange(static_cast<uint32_t>(houses.size()))];
		StationFinder stations(TileArea(tile, 1, 1));
		moved += MoveGoodsToStation(pass, part, Source::Make<SourceType::Town>(t->index), stations.GetStations());
	}
	return moved;
}

/** Daily: workers go from the residential districts to the jobs and back; the rest work in a neighbour town. */
static void UpdateCommuting()
{
	/* Workers and jobs of every active town with districts, computed once. */
	std::vector<std::tuple<const Town *, uint32_t, uint32_t>> towns;
	for (const Town *t : Town::Iterate()) {
		if (!MetroCityIsTownActive(t) || GetTownDistricts(t).empty()) continue;
		std::vector<MetroDistrictStats> stats = MetroTownGetDistrictStats(t);
		towns.emplace_back(t, stats[to_underlying(MetroDistrictType::Residential)].population * METRO_COMMUTE_WORKER_PERCENT / 100,
				stats[to_underlying(MetroDistrictType::Business)].population * METRO_COMMUTE_JOB_PERCENT / 100);
	}
	for (const auto &[t, workers, jobs] : towns) {
		uint32_t local = std::min(workers, jobs);
		bool neighbour_jobs = false;
		if (workers > jobs) {
			for (const auto &[other, other_workers, other_jobs] : towns) {
				if (other != t && other_jobs > other_workers && DistanceManhattan(t->xy, other->xy) <= METRO_MIGRATION_REGION_TILES) neighbour_jobs = true;
			}
		}
		uint32_t out = neighbour_jobs ? workers - jobs : 0;

		uint daily_local = local * METRO_COMMUTE_DAILY_PERMILLE / 1000;
		uint daily_out = out * METRO_COMMUTE_DAILY_PERMILLE / 1000;
		MetroTownLifeState &state = GetLifeState(t->index);
		/* To work from the residential districts, back home from the business districts. */
		state.commuters_month += SendFromDistrict(t, MetroDistrictType::Residential, daily_local);
		state.commuters_month += SendFromDistrict(t, MetroDistrictType::Business, daily_local);
		uint moved_out = SendFromDistrict(t, MetroDistrictType::Residential, daily_out);
		state.commuters_month += moved_out;
		state.commuters_out_month += moved_out;
	}
}

/* ---------------------------------------------------------------------------------------------- */
/* Migration                                                                                      */
/* ---------------------------------------------------------------------------------------------- */

/** Attractiveness of the town, 0..100: transport, service of the stations, services and rating. */
static uint ComputeAttractiveness(const Town *t)
{
	uint32_t transported = 0;
	uint32_t produced = 0;
	for (CargoType cargo : CargoSpec::town_production_cargo_mask[TownProductionEffect::Passengers]) {
		auto it = t->GetCargoSupplied(cargo);
		if (it == std::end(t->supplied)) continue;
		transported += it->history[LAST_MONTH].transported;
		produced += it->history[LAST_MONTH].production;
	}
	uint transport = produced == 0 ? 0 : std::min<uint>(100, transported * 100 / produced);

	uint rating_sum = 0;
	uint rating_count = 0;
	for (const Station *st : Station::Iterate()) {
		if (st->town != t || !Company::IsValidHumanID(st->owner)) continue;
		for (CargoType cargo : CargoSpec::town_production_cargo_mask[TownProductionEffect::Passengers]) {
			if (!st->goods[cargo].HasRating()) continue;
			rating_sum += st->goods[cargo].rating * 100 / 255;
			rating_count++;
		}
	}
	uint service = rating_count == 0 ? 0 : rating_sum / rating_count;

	uint allowed = 0;
	uint built = 0;
	for (uint i = 0; i < METRO_SERVICE_COUNT; i++) {
		MetroServiceType type = static_cast<MetroServiceType>(i);
		if (t->cache.population < GetMetroServiceMinPopulation(type)) continue;
		allowed++;
		if (CountMetroServices(t, type) > 0) built++;
	}
	uint services = allowed == 0 ? 100 : built * 100 / allowed;

	int best_rating = RATING_MINIMUM;
	for (const Company *c : Company::Iterate()) {
		if (!c->is_ai && t->have_ratings.Test(c->index)) best_rating = std::max<int>(best_rating, t->ratings[c->index]);
	}
	uint rating = static_cast<uint>((best_rating - RATING_MINIMUM) * 100 / (RATING_MAXIMUM - RATING_MINIMUM));

	return (transport * METRO_ATTRACT_TRANSPORT + service * METRO_ATTRACT_SERVICE + services * METRO_ATTRACT_SERVICES + rating * METRO_ATTRACT_RATING) / 100;
}

/** Every 5 months: compute the attractiveness and move people to better neighbour active towns. */
static void UpdateMigration()
{
	std::vector<Town *> towns;
	for (Town *t : Town::Iterate()) {
		if (!MetroCityIsTownActive(t)) continue;
		MetroTownLifeState &state = GetLifeState(t->index);
		state.attractiveness = static_cast<uint8_t>(ComputeAttractiveness(t));
		state.growth_effect = 0;
		towns.push_back(t);
	}
	CargoType pass = GetCargoTypeByLabel(CT_PASSENGERS);
	for (Town *t : towns) {
		MetroTownLifeState &state = GetLifeState(t->index);
		Town *best = nullptr;
		uint best_score = state.attractiveness + METRO_MIGRATION_MIN_GAP;
		for (Town *other : towns) {
			if (other == t || DistanceManhattan(t->xy, other->xy) > METRO_MIGRATION_REGION_TILES) continue;
			uint score = GetLifeState(other->index).attractiveness;
			if (score >= best_score) {
				best_score = score;
				best = other;
			}
		}
		if (best == nullptr) continue;
		uint gap = best_score - state.attractiveness;
		uint32_t movers = std::min<uint32_t>(t->cache.population * gap * METRO_MIGRATION_PERMILLE_PER_POINT / 1000, t->cache.population * METRO_MIGRATION_MAX_PERCENT / 100);
		if (movers == 0) continue;
		state.moved_out_year += movers;
		state.growth_effect = -1;
		MetroTownLifeState &dest = GetLifeState(best->index);
		dest.moved_in_year += movers;
		dest.growth_effect = 1;
		/* The movers travel by the player's transport. */
		if (IsValidCargoType(pass)) {
			uint parts = std::min<uint>(METRO_COMMUTE_SPREAD_HOUSES, movers);
			std::vector<TileIndex> houses;
			ForTownHouseTiles(t, [&](TileIndex tile) { houses.push_back(tile); });
			for (uint i = 0; i < parts && !houses.empty(); i++) {
				TileIndex tile = houses[RandomRange(static_cast<uint32_t>(houses.size()))];
				StationFinder stations(TileArea(tile, 1, 1));
				MoveGoodsToStation(pass, movers / parts, Source::Make<SourceType::Town>(t->index), stations.GetStations());
			}
		}
		InvalidateWindowData(WindowClass::TownView, t->index);
		InvalidateWindowData(WindowClass::TownView, best->index);
	}
}

uint MetroTownAdjustGrowthRate(const Town *t, uint rate)
{
	auto it = _metro_town_life.find(t->index.base());
	if (it == _metro_town_life.end() || !MetroCityIsTownActive(t)) return rate;
	if (it->second.growth_effect < 0) rate = rate * METRO_MIGRATION_OUTFLOW_GROWTH_PERCENT / 100;
	if (it->second.growth_effect > 0) rate = rate * METRO_MIGRATION_INFLOW_GROWTH_PERCENT / 100;
	return std::max(rate, 1u);
}

static const IntervalTimer<TimerGameCalendar> _metro_town_daily({TimerGameCalendar::Trigger::Day, TimerGameCalendar::Priority::None}, [](auto) {
	UpdateCommuting();
});

static const IntervalTimer<TimerGameCalendar> _metro_town_monthly({TimerGameCalendar::Trigger::Month, TimerGameCalendar::Priority::None}, [](auto) {
	for (auto &[town, state] : _metro_town_life) {
		state.commuters_last_month = state.commuters_month;
		state.commuters_out_last_month = state.commuters_out_month;
		state.commuters_month = 0;
		state.commuters_out_month = 0;
	}
	UpdateDistricts();
	CalTime::YearMonthDay ymd = CalTime::CurYMD();
	if ((ymd.year.base() * 12 + ymd.month) % METRO_MIGRATION_PERIOD_MONTHS == 0) UpdateMigration();
	SetWindowClassesDirty(WindowClass::TownView);
});

static const IntervalTimer<TimerGameCalendar> _metro_town_yearly({TimerGameCalendar::Trigger::Year, TimerGameCalendar::Priority::None}, [](auto) {
	for (auto &[town, state] : _metro_town_life) {
		state.moved_in_last_year = state.moved_in_year;
		state.moved_out_last_year = state.moved_out_year;
		state.moved_in_year = 0;
		state.moved_out_year = 0;
	}
});

/* ---------------------------------------------------------------------------------------------- */
/* Town window                                                                                    */
/* ---------------------------------------------------------------------------------------------- */

std::vector<std::string> MetroTownGetViewLines(const Town *t)
{
	std::vector<std::string> lines;
	if (!MetroCityIsTownActive(t)) return lines;
	std::vector<MetroDistrictStats> stats = MetroTownGetDistrictStats(t);
	const MetroDistrictStats &business = stats[to_underlying(MetroDistrictType::Business)];
	const MetroDistrictStats &residential = stats[to_underlying(MetroDistrictType::Residential)];
	if (business.districts + residential.districts > 0) {
		lines.push_back(GetString(STR_METRO_TOWN_DISTRICTS_LINE, business.districts, residential.districts));
		lines.push_back(GetString(STR_METRO_TOWN_JOBS_LINE, MetroTownGetWorkers(t), MetroTownGetJobs(t)));
	}
	auto it = _metro_town_life.find(t->index.base());
	if (it == _metro_town_life.end()) return lines;
	const MetroTownLifeState &state = it->second;
	if (state.commuters_last_month > 0) lines.push_back(GetString(STR_METRO_TOWN_COMMUTERS_LINE, state.commuters_last_month, state.commuters_out_last_month));
	lines.push_back(GetString(STR_METRO_TOWN_ATTRACT_LINE, state.attractiveness, state.moved_in_year, state.moved_out_year, state.moved_in_last_year, state.moved_out_last_year));
	return lines;
}

/* ---------------------------------------------------------------------------------------------- */
/* Console                                                                                        */
/* ---------------------------------------------------------------------------------------------- */

bool ConMetroTown(std::span<std::string_view> argv)
{
	if (argv.empty()) {
		IConsolePrint(CC_HELP, "Metro mod town life. Usage: metro_town info <town id> | districts <town id> | commute | migrate | view <town id>");
		IConsolePrint(CC_HELP, "  districts: create the due districts now; commute: run one day of commuting; migrate: run the migration now.");
		return true;
	}
	std::string_view cmd = argv.size() > 1 ? argv[1] : "info";
	auto town_arg = [&]() -> Town * {
		auto id = argv.size() > 2 ? ParseInteger<uint>(argv[2]) : std::nullopt;
		Town *t = id.has_value() ? Town::GetIfValid(*id) : nullptr;
		if (t == nullptr) IConsolePrint(CC_ERROR, "Invalid town id.");
		return t;
	};
	if (cmd == "info") {
		Town *t = town_arg();
		if (t == nullptr) return true;
		for (const MetroDistrict *d : GetTownDistricts(t)) {
			IConsolePrint(CC_DEFAULT, "district {} at ({}, {})", GetString(_metro_district_specs[d->type].name), TileX(TileIndex{d->tile}), TileY(TileIndex{d->tile}));
		}
		std::vector<MetroDistrictStats> stats = MetroTownGetDistrictStats(t);
		for (uint i = 0; i < stats.size(); i++) {
			IConsolePrint(CC_DEFAULT, "{}: districts {}, houses {}, residents {}", GetString(_metro_district_specs[i].name), stats[i].districts, stats[i].houses, stats[i].population);
		}
		IConsolePrint(CC_DEFAULT, "workers {}, jobs {}, attractiveness now {}", MetroTownGetWorkers(t), MetroTownGetJobs(t), ComputeAttractiveness(t));
		for (const std::string &line : MetroTownGetViewLines(t)) IConsolePrint(CC_DEFAULT, "{}", line);
		return true;
	}
	if (cmd == "districts") {
		UpdateDistricts();
		IConsolePrint(CC_INFO, "Districts: {}.", _metro_districts.size());
		return true;
	}
	if (cmd == "commute") {
		UpdateCommuting();
		IConsolePrint(CC_INFO, "One day of commuting done.");
		return true;
	}
	if (cmd == "migrate") {
		UpdateMigration();
		for (const auto &[town, state] : _metro_town_life) {
			IConsolePrint(CC_DEFAULT, "{}: attractiveness {}, in {}, out {}", GetString(STR_TOWN_NAME, TownID{town}), state.attractiveness, state.moved_in_year, state.moved_out_year);
		}
		return true;
	}
	if (cmd == "view") {
		Town *t = town_arg();
		if (t != nullptr) ShowMetroDistrictsWindow(t->index);
		return true;
	}
	IConsolePrint(CC_ERROR, "Unknown subcommand.");
	return true;
}
