/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_traffic.cpp Metro mod: private cars, traffic jams and pedestrians in active towns.
 *
 * Everything here is visual and not saved. Cars and pedestrians are simulated tile by tile only for active towns shown
 * in a viewport (plus a margin), and only while the settings allow them. Cars drive between houses and the town
 * centre without a path finder: at each tile centre they take the road that brings them closer to the target, wait
 * when the lane ahead is full and cross junctions one at a time.
 * Jams slow the player's road vehicles on the roads of active towns: in the detailed area by the cars on the tile,
 * everywhere else by an estimate of the town (cars per road tile, busier in the centre). Without cars there are no jams.
 * Random numbers are the interactive ones, so the game random stays untouched.
 */

#include "stdafx.h"
#include "metro_traffic.h"
#include "metro_city.h"
#include "company_base.h"
#include "console_func.h"
#include "console_type.h"
#include "date_func.h"
#include "direction_func.h"
#include "gfx_func.h"
#include "landscape.h"
#include "road_map.h"
#include "road_func.h"
#include "roadveh.h"
#include "settings_type.h"
#include "sprite.h"
#include "station_base.h"
#include "tile_cmd.h"
#include "town.h"
#include "town_map.h"
#include "viewport_func.h"
#include "window_gui.h"
#include "zoom_func.h"
#include "core/random_func.hpp"
#include "timer/timer.h"
#include "timer/timer_game_tick.h"
#include "table/metro_traffic_tables.h"

#include <unordered_map>

#include "safeguards.h"

/** A private car: drives on the road tile @p tile in direction @p dir; @p progress 0..15 from the entry edge. */
struct MetroCar {
	TileIndex tile;
	TileIndex target;
	uint16_t town;
	DiagDirection dir;
	uint8_t progress;
	uint8_t sprite;   ///< Index in #_metro_car_sprites.
	uint8_t colour;
	uint16_t tiles;   ///< Tiles driven.
};

/** A pedestrian walking on the sidewalk towards @p target; disappears there ("dives into the entrance"). */
struct MetroPedestrian {
	TileIndex tile;
	TileIndex target;
	uint16_t town;
	DiagDirection dir;
	uint8_t progress;
	uint8_t colour;
	uint16_t steps;
	bool side;        ///< Which sidewalk.
};

static std::vector<MetroCar> _cars;
static std::vector<MetroPedestrian> _pedestrians;
static std::unordered_multimap<uint32_t, uint32_t> _cars_on_tile;        ///< Tile -> index in #_cars, rebuilt every tick.
static std::unordered_multimap<uint32_t, uint32_t> _pedestrians_on_tile; ///< Tile -> index in #_pedestrians.
static std::unordered_map<uint16_t, bool> _towns_in_view;                ///< Active towns simulated in detail (value: pedestrians too).
static std::unordered_map<uint16_t, uint8_t> _town_jam_estimate;         ///< Slowdown in percent at the centre of each active town.

void MetroTrafficReset()
{
	_cars.clear();
	_pedestrians.clear();
	_cars_on_tile.clear();
	_pedestrians_on_tile.clear();
	_towns_in_view.clear();
	_town_jam_estimate.clear();
}

static bool CarsEnabled() { return _settings_game.economy.metro_town_cars; }
static bool PedestriansEnabled() { return _settings_game.economy.metro_pedestrians; }

/* ---------------------------------------------------------------------------------------------- */
/* Roads                                                                                          */
/* ---------------------------------------------------------------------------------------------- */

/** Road bits of a plain road tile (no depots, stops, bridges), or ROAD_NONE. */
static RoadBits GetCarRoadBits(TileIndex tile)
{
	if (!IsValidTile(tile) || !IsTileType(tile, TileType::Road) || !IsNormalRoadTile(tile)) return ROAD_NONE;
	return GetRoadBits(tile, RoadTramType::Road);
}

static bool IsJunction(TileIndex tile)
{
	return CountBits(GetCarRoadBits(tile)) >= 3;
}

/** Whether the road continues from @p tile towards @p dir (both tiles have the matching half). */
static bool CanDrive(TileIndex tile, DiagDirection dir)
{
	if ((GetCarRoadBits(tile) & DiagDirToRoadBits(dir)) == ROAD_NONE) return false;
	TileIndex next = TileAddByDiagDir(tile, dir);
	return (GetCarRoadBits(next) & DiagDirToRoadBits(ReverseDiagDir(dir))) != ROAD_NONE;
}

/** Choose the direction at a tile centre: towards the target, sometimes another way; no U-turn unless it is a dead end. */
static DiagDirection ChooseDirection(TileIndex tile, DiagDirection from_dir, TileIndex target)
{
	DiagDirection best = DiagDirection::Invalid;
	uint best_dist = UINT_MAX;
	DiagDirection options[to_underlying(DiagDirection::End)];
	uint count = 0;
	DiagDirection back = from_dir == DiagDirection::Invalid ? DiagDirection::Invalid : ReverseDiagDir(from_dir);
	for (DiagDirection dir : EnumRange(DiagDirection::End)) {
		if (dir == back || !CanDrive(tile, dir)) continue;
		options[count++] = dir;
		uint dist = DistanceManhattan(TileAddByDiagDir(tile, dir), target);
		if (dist < best_dist) {
			best_dist = dist;
			best = dir;
		}
	}
	if (count == 0) return (back != DiagDirection::Invalid && CanDrive(tile, back)) ? back : DiagDirection::Invalid;
	if (count > 1 && InteractiveRandomRange(4) == 0) return options[InteractiveRandomRange(count)];
	return best;
}

/** World pixel position of a mover on @p tile, going @p dir, @p progress pixels from the entry edge, @p offset to the right. */
static Point GetPosition(TileIndex tile, DiagDirection dir, uint progress, int offset)
{
	TileIndexDiffC d = TileIndexDiffCByDiagDir(dir);
	int along = static_cast<int>(progress) - static_cast<int>(TILE_SIZE / 2);
	/* Right-hand side of the direction (x, y) in map coordinates is (-y, x). */
	return {
		static_cast<int>(TileX(tile) * TILE_SIZE + TILE_SIZE / 2) + d.x * along - d.y * offset,
		static_cast<int>(TileY(tile) * TILE_SIZE + TILE_SIZE / 2) + d.y * along + d.x * offset,
	};
}

/* ---------------------------------------------------------------------------------------------- */
/* Which towns are on screen                                                                      */
/* ---------------------------------------------------------------------------------------------- */

/** Radius of the town in tiles, for the view checks and the estimate. */
static uint GetTownRadius(const Town *t)
{
	return IntSqrt(t->cache.squared_town_zone_radius[to_underlying(HouseZone::TownEdge)]) + 2;
}

/** Traffic of today's match of the town and its stadium (north tile), or nullptr when there is no match today. */
static const MetroMatchTrafficSpec *GetTownMatch(const Town *t, TileIndex &stadium)
{
	uint8_t level = 0;
	stadium = MetroCityGetMatchDayStadium(t->index, &level);
	if (stadium == INVALID_TILE || level >= std::size(_metro_match_traffic)) return nullptr;
	return &_metro_match_traffic[level];
}

/** A random road tile within @p radius tiles of the middle of the 2x2 stadium, or INVALID_TILE. */
static TileIndex RandomRoadNearStadium(TileIndex stadium)
{
	int radius = 3;
	for (uint attempt = 0; attempt < 16; attempt++) {
		int x = static_cast<int>(TileX(stadium)) + static_cast<int>(InteractiveRandomRange(2 * radius + 2)) - radius;
		int y = static_cast<int>(TileY(stadium)) + static_cast<int>(InteractiveRandomRange(2 * radius + 2)) - radius;
		if (x < 1 || y < 1 || x >= static_cast<int>(Map::MaxX()) || y >= static_cast<int>(Map::MaxY())) continue;
		TileIndex tile = TileXY(x, y);
		if (GetCarRoadBits(tile) != ROAD_NONE) return tile;
	}
	return INVALID_TILE;
}

/** Refresh the active towns shown in a viewport; pedestrians only at a close zoom. */
static void UpdateTownsInView()
{
	_towns_in_view.clear();
	if (IsHeadless()) return;
	for (const Window *w : Window::IterateFromBack()) {
		const Viewport *vp = w->viewport;
		if (vp == nullptr || vp->zoom > ZoomLevel::Out4x) continue;
		bool close = vp->zoom <= ZoomLevel::Normal;
		for (const Town *t : Town::Iterate()) {
			if (!MetroCityIsTownActive(t)) continue;
			int margin = (GetTownRadius(t) + METRO_CARS_VIEW_MARGIN_TILES) * TILE_PIXELS * 2;
			Point pt = RemapCoords2(TileX(t->xy) * TILE_SIZE, TileY(t->xy) * TILE_SIZE);
			if (pt.x + margin < vp->virtual_left || pt.x - margin > vp->virtual_left + vp->virtual_width ||
					pt.y + margin < vp->virtual_top || pt.y - margin > vp->virtual_top + vp->virtual_height) continue;
			bool &ped = _towns_in_view[t->index.base()];
			ped = ped || close;
		}
	}
}

/* ---------------------------------------------------------------------------------------------- */
/* Cars                                                                                           */
/* ---------------------------------------------------------------------------------------------- */

/** Share of the town's passengers carried last month, 0..100. */
static uint GetTownCoverage(const Town *t)
{
	uint32_t transported = 0;
	uint32_t produced = 0;
	for (CargoType cargo : CargoSpec::town_production_cargo_mask[TownProductionEffect::Passengers]) {
		auto it = t->GetCargoSupplied(cargo);
		if (it == std::end(t->supplied)) continue;
		transported += it->history[LAST_MONTH].transported;
		produced += it->history[LAST_MONTH].production;
	}
	return produced == 0 ? 0 : std::min<uint>(100, transported * 100 / produced);
}

/** Cars the town has: more residents, more cars; good transport, fewer cars. */
static uint GetTownCarCount(const Town *t)
{
	uint cars = t->cache.population / METRO_CARS_POPULATION_PER_CAR;
	cars = cars * (100 - GetTownCoverage(t) * METRO_CARS_COVERAGE_CUT_PERCENT / 100) / 100;
	uint limit = METRO_CARS_MAX_PER_TOWN;
	TileIndex stadium;
	if (const MetroMatchTrafficSpec *match = GetTownMatch(t, stadium); match != nullptr) {
		/* Match day: the fans come by car too. */
		cars = cars * match->car_percent / 100;
		limit = limit * match->car_percent / 100;
	}
	return std::min<uint>(cars, limit);
}

/** A random road tile next to a random house of the town, or INVALID_TILE. */
static TileIndex RandomRoadNearHouse(const Town *t)
{
	uint radius = GetTownRadius(t);
	for (uint attempt = 0; attempt < 8; attempt++) {
		int x = static_cast<int>(TileX(t->xy)) + static_cast<int>(InteractiveRandomRange(2 * radius + 1)) - static_cast<int>(radius);
		int y = static_cast<int>(TileY(t->xy)) + static_cast<int>(InteractiveRandomRange(2 * radius + 1)) - static_cast<int>(radius);
		if (x < 1 || y < 1 || x >= static_cast<int>(Map::MaxX()) || y >= static_cast<int>(Map::MaxY())) continue;
		TileIndex tile = TileXY(x, y);
		if (!IsTileType(tile, TileType::House) || GetTownIndex(tile) != t->index) continue;
		for (DiagDirection dir : EnumRange(DiagDirection::End)) {
			TileIndex road = TileAddByDiagDir(tile, dir);
			if (GetCarRoadBits(road) != ROAD_NONE) return road;
		}
	}
	return INVALID_TILE;
}

/** Nearest road tile to the town centre (the centre itself usually is one). */
static TileIndex GetTownCentreRoad(const Town *t)
{
	if (GetCarRoadBits(t->xy) != ROAD_NONE) return t->xy;
	for (DiagDirection dir : EnumRange(DiagDirection::End)) {
		TileIndex road = TileAddByDiagDir(t->xy, dir);
		if (GetCarRoadBits(road) != ROAD_NONE) return road;
	}
	return INVALID_TILE;
}

static void SpawnCar(const Town *t)
{
	TileIndex house_road = RandomRoadNearHouse(t);
	TileIndex centre = GetTownCentreRoad(t);
	TileIndex stadium;
	if (GetTownMatch(t, stadium) != nullptr && InteractiveRandomRange(100) < METRO_MATCH_TO_STADIUM_PERCENT) {
		/* Match day: to the stadium and back instead of the centre. */
		TileIndex road = RandomRoadNearStadium(stadium);
		if (road != INVALID_TILE) centre = road;
	}
	if (house_road == INVALID_TILE || centre == INVALID_TILE || house_road == centre) return;
	/* Half of the cars drive from home to the centre, half back. */
	bool to_centre = InteractiveRandomRange(2) == 0;
	TileIndex start = to_centre ? house_road : centre;
	TileIndex target = to_centre ? centre : house_road;
	DiagDirection dir = ChooseDirection(start, DiagDirection::Invalid, target);
	if (dir == DiagDirection::Invalid) return;

	int32_t year = CalTime::CurYear().base();
	std::vector<uint8_t> sprites;
	for (uint8_t i = 0; i < std::size(_metro_car_sprites); i++) {
		if (year >= _metro_car_sprites[i].from_year) sprites.push_back(i);
	}
	/* Newer models are more common: take from the newest half of the list. */
	uint8_t sprite = sprites[sprites.size() / 2 + InteractiveRandomRange(static_cast<uint32_t>(sprites.size() - sprites.size() / 2))];
	_cars.push_back({start, target, t->index.base(), dir, TILE_SIZE / 2, sprite, static_cast<uint8_t>(InteractiveRandomRange(METRO_CAR_COLOUR_COUNT)), 0});
}

/** Cars on the tile driving in the direction (or any direction when DiagDirection::Invalid). */
static uint CountCarsOn(TileIndex tile, DiagDirection dir)
{
	uint count = 0;
	auto [begin, end] = _cars_on_tile.equal_range(tile.base());
	for (auto it = begin; it != end; ++it) {
		if (dir == DiagDirection::Invalid || _cars[it->second].dir == dir) count++;
	}
	return count;
}

static void RebuildCarIndex()
{
	_cars_on_tile.clear();
	for (uint32_t i = 0; i < _cars.size(); i++) _cars_on_tile.emplace(_cars[i].tile.base(), i);
}

/** Move a car by one pixel; returns false when it has to disappear. */
static bool MoveCar(MetroCar &car)
{
	if (GetCarRoadBits(car.tile) == ROAD_NONE) return false;
	if (car.progress == TILE_SIZE / 2) {
		if (car.tile == car.target) return false;
		DiagDirection dir = ChooseDirection(car.tile, car.dir, car.target);
		if (dir == DiagDirection::Invalid) return false;
		car.dir = dir;
	}
	if (car.progress + 1u >= TILE_SIZE) {
		TileIndex next = TileAddByDiagDir(car.tile, car.dir);
		if (GetCarRoadBits(next) == ROAD_NONE) return false;
		/* Wait when the lane ahead is full or another car is in the junction. */
		if (CountCarsOn(next, car.dir) >= METRO_CARS_TILE_CAPACITY) return true;
		if (IsJunction(next) && CountCarsOn(next, DiagDirection::Invalid) > 0) return true;
		car.tile = next;
		car.progress = 0;
		if (++car.tiles > METRO_CARS_MAX_TILES) return false;
		return true;
	}
	car.progress++;
	return true;
}

static void TickCars()
{
	if (!CarsEnabled()) {
		if (!_cars.empty()) {
			for (const MetroCar &car : _cars) MarkTileDirtyByTile(car.tile);
			_cars.clear();
			_cars_on_tile.clear();
		}
		return;
	}
	/* Cars of towns that left the screen disappear. */
	std::erase_if(_cars, [](const MetroCar &car) {
		bool gone = !_towns_in_view.contains(car.town);
		if (gone) MarkTileDirtyByTile(car.tile);
		return gone;
	});

	if ((_tick_counter % METRO_CARS_STEP_TICKS) == 0) {
		std::vector<MetroCar> moved;
		moved.reserve(_cars.size());
		RebuildCarIndex();
		for (MetroCar &car : _cars) {
			TileIndex old_tile = car.tile;
			bool keep = MoveCar(car);
			MarkTileDirtyByTile(old_tile);
			if (keep) {
				if (car.tile != old_tile) MarkTileDirtyByTile(car.tile);
				moved.push_back(car);
			}
		}
		_cars.swap(moved);
	}

	/* Spawn cars of the towns on screen up to their number. */
	std::unordered_map<uint16_t, uint> per_town;
	for (const MetroCar &car : _cars) per_town[car.town]++;
	uint spawned = 0;
	for (const auto &[town, pedestrians] : _towns_in_view) {
		if (spawned >= METRO_CARS_SPAWN_PER_TICK || _cars.size() >= METRO_CARS_MAX_TOTAL) break;
		const Town *t = Town::GetIfValid(town);
		if (t == nullptr || per_town[town] >= GetTownCarCount(t)) continue;
		SpawnCar(t);
		spawned++;
	}
	RebuildCarIndex();
}

/* ---------------------------------------------------------------------------------------------- */
/* Pedestrians                                                                                    */
/* ---------------------------------------------------------------------------------------------- */

/** Passengers picked up in the town last month (flow of its stations). */
static uint32_t GetTownFlow(const Town *t)
{
	uint32_t transported = 0;
	for (CargoType cargo : CargoSpec::town_production_cargo_mask[TownProductionEffect::Passengers]) {
		auto it = t->GetCargoSupplied(cargo);
		if (it != std::end(t->supplied)) transported += it->history[LAST_MONTH].transported;
	}
	return transported;
}

static uint GetTownPedestrianCount(const Town *t)
{
	uint count = t->cache.population / METRO_PEDESTRIANS_POPULATION_PER_PERSON + GetTownFlow(t) / METRO_PEDESTRIANS_FLOW_PER_PERSON;
	count = std::min<uint>(count, METRO_PEDESTRIANS_MAX_PER_TOWN);
	TileIndex stadium;
	if (const MetroMatchTrafficSpec *match = GetTownMatch(t, stadium); match != nullptr) count += match->crowd; // Fans around the stadium.
	return count;
}

/** A station tile of a human player near @p tile (stops and station entrances), or INVALID_TILE. */
static TileIndex FindNearbyStation(TileIndex tile)
{
	TileIndex best = INVALID_TILE;
	uint best_dist = METRO_PEDESTRIANS_STATION_RADIUS + 1;
	for (const Station *st : Station::Iterate()) {
		if (!Company::IsValidHumanID(st->owner)) continue;
		uint dist = DistanceManhattan(st->xy, tile);
		if (dist < best_dist) {
			best_dist = dist;
			best = st->xy;
		}
	}
	return best;
}

static void SpawnPedestrian(const Town *t)
{
	TileIndex road = RandomRoadNearHouse(t);
	if (road == INVALID_TILE) return;
	TileIndex stadium;
	if (GetTownMatch(t, stadium) != nullptr && InteractiveRandomRange(100) < METRO_MATCH_TO_STADIUM_PERCENT) {
		/* Match day crowd: fans walk from home to the stadium, or from the stadium to a station. */
		TileIndex at_stadium = RandomRoadNearStadium(stadium);
		if (at_stadium == INVALID_TILE) return;
		TileIndex start = road;
		TileIndex target = at_stadium;
		if (InteractiveRandomRange(2) == 0) {
			TileIndex station = FindNearbyStation(at_stadium);
			if (station != INVALID_TILE) {
				start = at_stadium;
				target = station;
			}
		}
		DiagDirection dir = ChooseDirection(start, DiagDirection::Invalid, target);
		if (dir == DiagDirection::Invalid) return;
		_pedestrians.push_back({start, target, t->index.base(), dir, TILE_SIZE / 2, static_cast<uint8_t>(InteractiveRandomRange(METRO_CAR_COLOUR_COUNT)), 0, InteractiveRandomRange(2) == 0});
		return;
	}
	TileIndex station = FindNearbyStation(road);
	if (station == INVALID_TILE) return;
	/* Most walk to the station; some come out of it and walk home. */
	bool to_station = InteractiveRandomRange(3) != 0;
	TileIndex start = road;
	TileIndex target = station;
	if (!to_station) {
		/* Start on the road next to the station. */
		start = INVALID_TILE;
		for (DiagDirection dir : EnumRange(DiagDirection::End)) {
			TileIndex next = TileAddByDiagDir(station, dir);
			if (GetCarRoadBits(next) != ROAD_NONE) start = next;
		}
		if (start == INVALID_TILE) return;
		target = road;
	}
	DiagDirection dir = ChooseDirection(start, DiagDirection::Invalid, target);
	if (dir == DiagDirection::Invalid) return;
	_pedestrians.push_back({start, target, t->index.base(), dir, TILE_SIZE / 2, static_cast<uint8_t>(InteractiveRandomRange(METRO_CAR_COLOUR_COUNT)), 0, InteractiveRandomRange(2) == 0});
}

/** Move a pedestrian by one pixel; returns false when it arrives or gets lost. */
static bool MovePedestrian(MetroPedestrian &p)
{
	if (++p.steps > METRO_PEDESTRIANS_MAX_STEPS) return false;
	if (p.progress == TILE_SIZE / 2) {
		if (DistanceManhattan(p.tile, p.target) <= 1) return false; // Dives into the entrance or the house.
		if (GetCarRoadBits(p.tile) == ROAD_NONE) return false;
		DiagDirection dir = ChooseDirection(p.tile, p.dir, p.target);
		if (dir == DiagDirection::Invalid) return false;
		p.dir = dir;
	}
	if (p.progress + 1u >= TILE_SIZE) {
		p.tile = TileAddByDiagDir(p.tile, p.dir);
		p.progress = 0;
		return IsValidTile(p.tile);
	}
	p.progress++;
	return true;
}

static void TickPedestrians()
{
	if (!PedestriansEnabled()) {
		if (!_pedestrians.empty()) {
			for (const MetroPedestrian &p : _pedestrians) MarkTileDirtyByTile(p.tile);
			_pedestrians.clear();
			_pedestrians_on_tile.clear();
		}
		return;
	}
	std::erase_if(_pedestrians, [](const MetroPedestrian &p) {
		auto it = _towns_in_view.find(p.town);
		bool gone = it == _towns_in_view.end() || !it->second;
		if (gone) MarkTileDirtyByTile(p.tile);
		return gone;
	});
	if ((_tick_counter % METRO_PEDESTRIANS_STEP_TICKS) == 0) {
		std::vector<MetroPedestrian> moved;
		moved.reserve(_pedestrians.size());
		for (MetroPedestrian &p : _pedestrians) {
			TileIndex old_tile = p.tile;
			bool keep = MovePedestrian(p);
			MarkTileDirtyByTile(old_tile);
			if (keep) moved.push_back(p);
		}
		_pedestrians.swap(moved);

		std::unordered_map<uint16_t, uint> per_town;
		for (const MetroPedestrian &p : _pedestrians) per_town[p.town]++;
		for (const auto &[town, close] : _towns_in_view) {
			if (!close || _pedestrians.size() >= METRO_PEDESTRIANS_MAX_TOTAL) continue;
			const Town *t = Town::GetIfValid(town);
			if (t == nullptr) continue;
			TileIndex stadium;
			uint limit = GetTownMatch(t, stadium) != nullptr ? METRO_MATCH_SPAWN_PER_STEP : METRO_PEDESTRIANS_SPAWN_PER_STEP;
			uint wanted = GetTownPedestrianCount(t);
			for (uint i = 0; i < limit && per_town[town] < wanted && _pedestrians.size() < METRO_PEDESTRIANS_MAX_TOTAL; i++) {
				size_t before = _pedestrians.size();
				SpawnPedestrian(t);
				if (_pedestrians.size() > before) per_town[town]++;
			}
		}
	}
	_pedestrians_on_tile.clear();
	for (uint32_t i = 0; i < _pedestrians.size(); i++) _pedestrians_on_tile.emplace(_pedestrians[i].tile.base(), i);
}

/* ---------------------------------------------------------------------------------------------- */
/* Jams                                                                                           */
/* ---------------------------------------------------------------------------------------------- */

/** Estimate of the slowdown in the centre of an active town: its cars per road tile. */
static void UpdateJamEstimate(const Town *t)
{
	_town_jam_estimate.erase(t->index.base());
	if (!CarsEnabled() || !MetroCityIsTownActive(t)) return;
	uint cars = GetTownCarCount(t);
	if (cars == 0) return;
	uint radius = GetTownRadius(t);
	uint roads = 0;
	for (TileIndex tile : SpiralTileSequence(t->xy, 2 * radius + 1)) {
		if (GetCarRoadBits(tile) != ROAD_NONE) roads++;
	}
	if (roads == 0) return;
	uint slowdown = cars * METRO_JAM_SLOWDOWN_PER_CAR / roads;
	_town_jam_estimate[t->index.base()] = static_cast<uint8_t>(std::min<uint>(slowdown, METRO_JAM_MAX_SLOWDOWN));
}

/** Each tick a slice of the towns gets its estimate, so every town is refreshed once per #METRO_JAM_ESTIMATE_TICKS. */
static void UpdateJamEstimates(uint slice, uint slices)
{
	if (!CarsEnabled()) {
		_town_jam_estimate.clear();
		return;
	}
	for (const Town *t : Town::Iterate()) {
		if (t->index.base() % slices == slice) UpdateJamEstimate(t);
	}
}

int MetroTrafficAdjustRoadVehicleSpeed(const RoadVehicle *v, int max_speed)
{
	if (_town_jam_estimate.empty() && _cars_on_tile.empty()) return max_speed;
	if (!Company::IsValidHumanID(v->owner)) return max_speed;
	TileIndex tile = v->tile;
	if (RoadTypeIsTram(v->roadtype) && IsTileType(tile, TileType::Road) && GetRoadTypeTram(tile) == ROADTYPE_METRO_TRAM) return max_speed; // Dedicated tram track.
	if (GetCarRoadBits(tile) == ROAD_NONE) return max_speed; // Tram-only tiles, stations, depots, bridges: no jams.
	uint16_t town = GetTownIndex(tile).base();
	uint slowdown = 0;
	if (auto it = _town_jam_estimate.find(town); it != _town_jam_estimate.end()) {
		const Town *t = Town::GetIfValid(town);
		if (t != nullptr) {
			bool centre = DistanceMax(t->xy, tile) * 2 <= GetTownRadius(t);
			slowdown = it->second * (centre ? METRO_JAM_CENTRE_PERCENT : METRO_JAM_OUTSKIRTS_PERCENT) / 100;
			TileIndex stadium;
			const MetroMatchTrafficSpec *match = GetTownMatch(t, stadium);
			if (match != nullptr && DistanceMax(stadium, tile) <= METRO_MATCH_AREA_TILES) slowdown += match->jam_percent;
		}
	}
	/* On screen: the cars around the vehicle. */
	slowdown = std::max(slowdown, CountCarsOn(tile, DiagDirection::Invalid) * METRO_JAM_SLOWDOWN_PER_CAR);
	slowdown = std::min<uint>(slowdown, METRO_JAM_MAX_SLOWDOWN);
	return slowdown == 0 ? max_speed : std::max(1, max_speed * static_cast<int>(100 - slowdown) / 100);
}

/* ---------------------------------------------------------------------------------------------- */
/* Ticks and drawing                                                                              */
/* ---------------------------------------------------------------------------------------------- */

static const IntervalTimer<TimerGameTick> _metro_traffic_ticks({TimerGameTick::Priority::None, 1}, [](auto) {
	if ((_tick_counter % 32) == 0) UpdateTownsInView();
	UpdateJamEstimates(_tick_counter % METRO_JAM_ESTIMATE_TICKS, METRO_JAM_ESTIMATE_TICKS);
	TickCars();
	TickPedestrians();
});

void MetroTrafficDrawTile(const TileInfo *ti)
{
	if (_cars_on_tile.empty() && _pedestrians_on_tile.empty()) return;
	bool right = _settings_game.vehicle.road_side == RoadVehicleDrivingSide::Right;

	auto [cb, ce] = _cars_on_tile.equal_range(ti->tile.base());
	for (auto it = cb; it != ce; ++it) {
		const MetroCar &car = _cars[it->second];
		Point pos = GetPosition(car.tile, car.dir, car.progress, right ? METRO_CARS_LANE_OFFSET : -METRO_CARS_LANE_OFFSET);
		int z = GetSlopePixelZ(pos.x, pos.y);
		SpriteID sprite = _metro_car_sprites[car.sprite].sprite + to_underlying(DiagDirToDir(car.dir));
		AddSortableSpriteToDraw(sprite, GetColourPalette(static_cast<Colours>(car.colour)), pos.x, pos.y, z, SpriteBounds({-1, -1, 0}, {3, 3, 6}, {1, 1, 0}));
	}

	/* Pedestrians only at a close zoom. */
	if (_cur_dpi == nullptr || _cur_dpi->zoom > ZoomLevel::Normal) return;
	auto [pb, pe] = _pedestrians_on_tile.equal_range(ti->tile.base());
	for (auto it = pb; it != pe; ++it) {
		const MetroPedestrian &p = _pedestrians[it->second];
		Point pos = GetPosition(p.tile, p.dir, p.progress, p.side ? METRO_PEDESTRIANS_SIDEWALK_OFFSET : -METRO_PEDESTRIANS_SIDEWALK_OFFSET);
		int z = GetSlopePixelZ(pos.x, pos.y);
		AddSortableSpriteToDraw(SPR_BLOT, GetColourPalette(static_cast<Colours>(p.colour)), pos.x, pos.y, z + 2, SpriteBounds({0, 0, 0}, {1, 1, 4}, {}));
	}
}

/* ---------------------------------------------------------------------------------------------- */
/* Console                                                                                        */
/* ---------------------------------------------------------------------------------------------- */

bool ConMetroTraffic(std::span<std::string_view> argv)
{
	if (argv.empty()) {
		IConsolePrint(CC_HELP, "Metro mod town traffic. Usage: metro_traffic status");
		return true;
	}
	UpdateTownsInView();
	for (uint slice = 0; slice < METRO_JAM_ESTIMATE_TICKS; slice++) UpdateJamEstimates(slice, METRO_JAM_ESTIMATE_TICKS);
	IConsolePrint(CC_INFO, "cars {} (setting {}), pedestrians {} (setting {}), towns on screen {}", _cars.size(), CarsEnabled() ? "on" : "off",
			_pedestrians.size(), PedestriansEnabled() ? "on" : "off", _towns_in_view.size());
	for (const auto &[town, slowdown] : _town_jam_estimate) {
		const Town *t = Town::Get(town);
		TileIndex stadium;
		const MetroMatchTrafficSpec *match = GetTownMatch(t, stadium);
		uint centre = std::min<uint>(slowdown * METRO_JAM_CENTRE_PERCENT / 100, METRO_JAM_MAX_SLOWDOWN);
		IConsolePrint(CC_DEFAULT, "town {} at ({}, {}): cars wanted {}, pedestrians wanted {}, jam in the centre {}%{}", town, TileX(t->xy), TileY(t->xy), GetTownCarCount(t),
				GetTownPedestrianCount(t), centre, _towns_in_view.contains(town) ? ", on screen" : "");
		if (match != nullptr) IConsolePrint(CC_DEFAULT, "  match day at ({}, {}): +{}% jam near the stadium", TileX(stadium), TileY(stadium), match->jam_percent);
	}
	return true;
}
