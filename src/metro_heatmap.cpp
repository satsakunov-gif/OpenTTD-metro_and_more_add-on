/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_heatmap.cpp Metro mod: heat map and the other map modes (stage 9, decisions of the user).
 *
 * Every station counts the cargo its vehicles load and unload and the income of the deliveries, by vehicle type and
 * passengers / other cargo, and the cargo transferred there; 12 monthly totals are kept (#_metro_station_traffic,
 * saved). The heat map mode of the small map has a row of modes (the first legend row switches them): each mode tints
 * tiles from blue to red by its data — stations over their catchment, towns over their zone, the grid over the radius
 * of the substations — or by categories with their own colours. The flow modes also draw the vanilla link lines of
 * their cargo coloured by volume and mark stations with transfers. Filters (legend rows): vehicle type, route (the
 * stations in the orders of a vehicle, set from its orders window) and period (last month or last 12 months).
 */

#include "stdafx.h"
#include "metro_heatmap.h"
#include "metro_city.h"
#include "metro_power.h"
#include "metro_stations.h"
#include "metro_town.h"
#include "metro_traffic.h"
#include "metro_underground.h"
#include "station_base.h"
#include "town.h"
#include "vehicle_base.h"
#include "order_base.h"
#include "object_base.h"
#include "rail.h"
#include "rail_map.h"
#include "road_map.h"
#include "town_map.h"
#include "cargotype.h"
#include "company_func.h"
#include "bitmap_type.h"
#include "console_func.h"
#include "console_type.h"
#include "strings_func.h"
#include "palette_func.h"
#include "window_func.h"
#include "core/math_func.hpp"
#include "timer/timer.h"
#include "timer/timer_game_calendar.h"
#include "date_func.h"
#include "core/string_consumer.hpp"
#include "table/strings.h"

#include <cmath>

#include "safeguards.h"

std::map<uint16_t, MetroStationTraffic> _metro_station_traffic;

/** Colours of the levels 1..6 of the scalar modes, from blue to red. */
static const std::array<PixelColour, METRO_HEAT_LEVELS> _metro_heat_colours = { PC_DARK_BLUE, PC_LIGHT_BLUE, PC_GREEN, PC_YELLOW, PC_ORANGE, PC_RED };
/** The same reversed: modes where more is better (red = bad). */
static const std::array<PixelColour, METRO_HEAT_LEVELS> _metro_good_colours = { PC_RED, PC_ORANGE, PC_YELLOW, PC_GREEN, PC_LIGHT_BLUE, PC_DARK_BLUE };

/* Mode and filters of the view (not saved). */
static MetroMapMode _heat_mode = MetroMapMode::PassengerFlow;
static uint8_t _heat_transport = 0;               ///< 0 = all, 1..4 = train, road vehicle, ship, aircraft.
static uint8_t _heat_period = 0;                  ///< 0 = last month, 1 = last 12 months.
static VehicleID _heat_route = VehicleID::Invalid(); ///< Only the stations in the orders of this vehicle.

static std::vector<uint8_t> _heat_levels;   ///< Level 0..6 per tile of the last preparation.
static bool _heat_dirty = true;
static uint64_t _heat_prepared_tick = 0;    ///< Tick of the last preparation (live modes are refreshed daily).
static int64_t _heat_max = 0;               ///< Largest value of the last preparation (scalar modes).
static std::array<PixelColour, METRO_HEAT_LEVELS> _heat_colours = _metro_heat_colours;
static std::array<std::string, METRO_HEAT_LEVELS> _heat_texts;  ///< Legend text of each level.

/** Names of the modes, in the order of #MetroMapMode. */
static const StringID _heat_mode_names[] = {
	STR_METRO_MAP_MODE_PASSENGERS, STR_METRO_MAP_MODE_FREIGHT, STR_METRO_MAP_MODE_STATION_LOAD, STR_METRO_MAP_MODE_STATION_RATING,
	STR_METRO_MAP_MODE_JAMS, STR_METRO_MAP_MODE_PROFIT, STR_METRO_MAP_MODE_COVERAGE, STR_METRO_MAP_MODE_METRO_LAYERS,
	STR_METRO_MAP_MODE_VENTILATION, STR_METRO_MAP_MODE_GRID, STR_METRO_MAP_MODE_GRID_LOAD, STR_METRO_MAP_MODE_ELECTRIFICATION,
	STR_METRO_MAP_MODE_ACTIVE_TOWNS, STR_METRO_MAP_MODE_TOWN_RATING, STR_METRO_MAP_MODE_DISTRICTS, STR_METRO_MAP_MODE_TOURISTS,
	STR_METRO_MAP_MODE_MIGRATION,
};
static_assert(std::size(_heat_mode_names) == to_underlying(MetroMapMode::End));

void MetroHeatReset()
{
	_metro_station_traffic.clear();
	_heat_levels.clear();
	_heat_route = VehicleID::Invalid();
	_heat_dirty = true;
}

void MetroHeatOnStationDeleted(StationID station)
{
	if (_metro_station_traffic.erase(station.base()) != 0) _heat_dirty = true;
}

/** Category index of a vehicle type and cargo, or -1 for vehicles that don't count. */
static int Category(VehicleType type, CargoType cargo)
{
	int t;
	switch (type) {
		case VehicleType::Train: t = 0; break;
		case VehicleType::Road: t = 1; break;
		case VehicleType::Ship: t = 2; break;
		case VehicleType::Aircraft: t = 3; break;
		default: return -1;
	}
	return t * 2 + (IsCargoInClass(cargo, CargoClass::Passengers) ? 0 : 1);
}

/** Cargo loaded or unloaded at a station by a vehicle (@p transfers of it unloaded as a transfer). */
void MetroHeatRecord(const Station *st, VehicleType type, CargoType cargo, uint amount, uint transfers)
{
	if (amount == 0) return;
	int c = Category(type, cargo);
	if (c < 0) return;
	MetroStationTraffic &t = _metro_station_traffic[st->index.base()];
	t.month[c] += amount;
	t.transfers_month += transfers;
}

/** Income of cargo delivered at a station by a vehicle (the profitability mode). */
void MetroHeatRecordIncome(const Station *st, VehicleType type, CargoType cargo, Money income)
{
	if (st == nullptr || income == 0) return;
	int c = Category(type, cargo);
	if (c < 0) return;
	_metro_station_traffic[st->index.base()].income_month[c] += income;
}

/** A new month: the current totals become last month's. */
static void RotateTraffic()
{
	for (auto &[station, t] : _metro_station_traffic) {
		for (uint m = METRO_HEAT_MONTHS - 1; m > 0; m--) {
			t.history[m] = t.history[m - 1];
			t.transfers[m] = t.transfers[m - 1];
			t.income[m] = t.income[m - 1];
		}
		t.history[0] = t.month;
		t.transfers[0] = t.transfers_month;
		t.income[0] = t.income_month;
		t.month.fill(0);
		t.transfers_month = 0;
		t.income_month.fill(0);
	}
	_heat_dirty = true;
	SetWindowClassesDirty(WindowClass::SmallMap);
}

static const IntervalTimer<TimerGameCalendar> _metro_heat_monthly({TimerGameCalendar::Trigger::Month, TimerGameCalendar::Priority::None}, [](auto) {
	RotateTraffic();
});

/* ---------------------------------------------------------------------------------------------- */
/* Filters                                                                                        */
/* ---------------------------------------------------------------------------------------------- */

static bool IsFlowMode(MetroMapMode mode)
{
	return mode == MetroMapMode::PassengerFlow || mode == MetroMapMode::FreightFlow;
}

/** Whether the mode looks at stations (the transport and route filters apply). */
static bool IsStationMode(MetroMapMode mode)
{
	switch (mode) {
		case MetroMapMode::PassengerFlow: case MetroMapMode::FreightFlow: case MetroMapMode::StationLoad:
		case MetroMapMode::StationRating: case MetroMapMode::Profit: case MetroMapMode::Coverage:
			return true;
		default:
			return false;
	}
}

/** Whether the mode has monthly history (the period filter applies). */
static bool HasPeriod(MetroMapMode mode)
{
	return IsFlowMode(mode) || mode == MetroMapMode::Profit;
}

/** Whether the data of the mode changes during the month (refreshed daily). */
static bool IsLiveMode(MetroMapMode mode)
{
	return !HasPeriod(mode);
}

/** Whether the category passes the filters of the mode. */
static bool CategoryShown(uint c)
{
	if (_heat_transport != 0 && c / 2 != _heat_transport - 1u) return false;
	if (_heat_mode == MetroMapMode::PassengerFlow && (c % 2) != 0) return false;
	if (_heat_mode == MetroMapMode::FreightFlow && (c % 2) != 1) return false;
	return true;
}

/** Whether the station has a facility of the chosen vehicle type. */
static bool StationHasTransport(const Station *st)
{
	switch (_heat_transport) {
		case 1: return st->facilities.Test(StationFacility::Train);
		case 2: return st->facilities.Any({StationFacility::BusStop, StationFacility::TruckStop});
		case 3: return st->facilities.Test(StationFacility::Dock);
		case 4: return st->facilities.Test(StationFacility::Airport);
		default: return true;
	}
}

/** Stations in the orders of the route vehicle; empty without a route. */
static std::vector<StationID> RouteStations()
{
	std::vector<StationID> stations;
	const Vehicle *v = Vehicle::GetIfValid(_heat_route);
	if (v == nullptr) {
		_heat_route = VehicleID::Invalid();
		return stations;
	}
	for (VehicleOrderID i = 0; i < v->GetNumOrders(); i++) {
		const Order *o = v->GetOrder(i);
		if (o != nullptr && (o->IsType(OT_GOTO_STATION) || o->IsType(OT_IMPLICIT))) stations.push_back(o->GetDestination().ToStationID());
	}
	return stations;
}

/** Traffic of a station in the chosen period through the filters. */
static uint32_t StationValue(const MetroStationTraffic &t)
{
	uint64_t sum = 0;
	uint months = _heat_period == 0 ? 1 : METRO_HEAT_MONTHS;
	for (uint m = 0; m < months; m++) {
		for (uint c = 0; c < METRO_HEAT_CATEGORIES; c++) {
			if (CategoryShown(c)) sum += t.history[m][c];
		}
	}
	return static_cast<uint32_t>(std::min<uint64_t>(sum, UINT32_MAX));
}

static int64_t StationIncome(const MetroStationTraffic &t)
{
	int64_t sum = 0;
	uint months = _heat_period == 0 ? 1 : METRO_HEAT_MONTHS;
	for (uint m = 0; m < months; m++) {
		for (uint c = 0; c < METRO_HEAT_CATEGORIES; c++) {
			if (CategoryShown(c)) sum += t.income[m][c];
		}
	}
	return sum;
}

static uint32_t StationTransfers(const MetroStationTraffic &t)
{
	uint32_t sum = 0;
	uint months = _heat_period == 0 ? 1 : METRO_HEAT_MONTHS;
	for (uint m = 0; m < months; m++) sum += t.transfers[m];
	return sum;
}

/* ---------------------------------------------------------------------------------------------- */
/* Levels                                                                                         */
/* ---------------------------------------------------------------------------------------------- */

/** Level 1..6 of a value (logarithmic up to the largest one). */
static uint8_t LevelOf(int64_t value)
{
	if (value <= 0 || _heat_max <= 0) return 0;
	if (_heat_max <= 1) return METRO_HEAT_LEVELS;
	double f = std::log(static_cast<double>(value)) / std::log(static_cast<double>(_heat_max));
	uint level = 1 + static_cast<uint>(f * (METRO_HEAT_LEVELS - 1) + 0.5);
	return static_cast<uint8_t>(std::clamp<uint>(level, 1, METRO_HEAT_LEVELS));
}

/** Lowest value of a level of the logarithmic scale. */
static int64_t LevelThreshold(uint level)
{
	if (_heat_max <= 1 || level <= 1) return _heat_max <= 0 ? 0 : 1;
	double f = (level - 1.5) / (METRO_HEAT_LEVELS - 1);
	return static_cast<int64_t>(std::ceil(std::pow(static_cast<double>(_heat_max), f)));
}

/** Legend of a logarithmic scale: "from N" per level, @p money for amounts of money. */
static void SetLogLegend(bool money)
{
	for (uint level = 1; level <= METRO_HEAT_LEVELS; level++) {
		if (_heat_max <= 0) {
			_heat_texts[level - 1] = level == 1 ? GetString(STR_METRO_HEAT_NO_TRAFFIC) : std::string{};
		} else {
			_heat_texts[level - 1] = money ? GetString(STR_METRO_HEAT_LEVEL_MONEY, LevelThreshold(level)) : GetString(STR_METRO_HEAT_LEVEL, LevelThreshold(level));
		}
	}
}

/** Level by fixed bands: the first band whose upper bound (inclusive) holds the value; the last band takes the rest. */
template <size_t N>
static uint8_t BandOf(int64_t value, const std::array<int64_t, N> &upper)
{
	for (size_t i = 0; i < N; i++) {
		if (value <= upper[i]) return static_cast<uint8_t>(i + 1);
	}
	return static_cast<uint8_t>(N + 1);
}

/** Set a categorical legend: label strings and colours of the levels (STR_NULL = level not used). */
static void SetCategories(std::initializer_list<std::pair<StringID, PixelColour>> categories)
{
	uint i = 0;
	for (const auto &[str, colour] : categories) {
		_heat_colours[i] = colour;
		_heat_texts[i] = str == STR_NULL ? std::string{} : GetString(STR_METRO_HEAT_CATEGORY, str);
		i++;
	}
	for (; i < METRO_HEAT_LEVELS; i++) {
		_heat_colours[i] = PC_BLACK;
		_heat_texts[i].clear();
	}
}

static void Paint(TileIndex tile, uint8_t level)
{
	if (level == 0 || tile.base() >= _heat_levels.size()) return;
	uint8_t &l = _heat_levels[tile.base()];
	l = std::max(l, level);
}

/** Paint a square of @p radius tiles around @p centre. */
static void PaintSquare(TileIndex centre, uint radius, uint8_t level)
{
	int cx = TileX(centre), cy = TileY(centre), r = radius;
	for (int y = std::max(0, cy - r); y <= std::min<int>(Map::MaxY(), cy + r); y++) {
		for (int x = std::max(0, cx - r); x <= std::min<int>(Map::MaxX(), cx + r); x++) Paint(TileXY(x, y), level);
	}
}

/** Paint the catchment of a station (its sign tile without one). */
static void PaintStation(const Station *st, uint8_t level)
{
	bool any = false;
	for (BitmapTileIterator it(st->catchment_tiles); it != INVALID_TILE; ++it) {
		Paint(TileIndex(it), level);
		any = true;
	}
	if (!any) Paint(st->xy, level);
}

/** Call @p f for every tile of the zone of the town (its local authority radius, at least 2 tiles). */
template <typename F>
static void ForTownZone(const Town *t, F f)
{
	uint32_t r2 = std::max<uint32_t>(t->cache.squared_town_zone_radius[to_underlying(HouseZone::TownEdge)], 4);
	int r = IntSqrt(r2);
	int cx = TileX(t->xy), cy = TileY(t->xy);
	for (int y = std::max(0, cy - r); y <= std::min<int>(Map::MaxY(), cy + r); y++) {
		for (int x = std::max(0, cx - r); x <= std::min<int>(Map::MaxX(), cx + r); x++) {
			if (static_cast<uint32_t>((x - cx) * (x - cx) + (y - cy) * (y - cy)) <= r2) f(TileXY(x, y));
		}
	}
}

static void PaintTownZone(const Town *t, uint8_t level)
{
	if (level == 0) return;
	ForTownZone(t, [&](TileIndex tile) { Paint(tile, level); });
}

/* ---------------------------------------------------------------------------------------------- */
/* Modes                                                                                          */
/* ---------------------------------------------------------------------------------------------- */

/** Stations the station modes look at: through the transport and route filters; @p own_only for the player's. */
template <typename F>
static void ForFilteredStations(bool own_only, F f)
{
	std::vector<StationID> route = RouteStations();
	for (const Station *st : Station::Iterate()) {
		if (own_only && st->owner != _local_company) continue;
		if (!route.empty() && std::ranges::find(route, st->index) == route.end()) continue;
		f(st);
	}
}

static void PrepareFlow()
{
	std::vector<std::pair<const Station *, int64_t>> values;
	std::vector<StationID> route = RouteStations();
	for (const auto &[station, t] : _metro_station_traffic) {
		const Station *st = Station::GetIfValid(StationID{station});
		if (st == nullptr) continue;
		if (!route.empty() && std::ranges::find(route, st->index) == route.end()) continue;
		int64_t v = StationValue(t);
		if (v > 0) values.emplace_back(st, v);
	}
	for (const auto &[st, v] : values) _heat_max = std::max(_heat_max, v);
	for (const auto &[st, v] : values) PaintStation(st, LevelOf(v));
	SetLogLegend(false);
}

static void PrepareStationLoad()
{
	std::vector<std::pair<const Station *, int64_t>> values;
	ForFilteredStations(true, [&](const Station *st) {
		if (!StationHasTransport(st)) return;
		int64_t waiting = 0;
		for (const GoodsEntry &ge : st->goods) waiting += ge.CargoTotalCount();
		if (waiting > 0) values.emplace_back(st, waiting);
	});
	for (const auto &[st, v] : values) _heat_max = std::max(_heat_max, v);
	for (const auto &[st, v] : values) PaintStation(st, LevelOf(v));
	SetLogLegend(false);
	if (_heat_max <= 0) _heat_texts[0] = GetString(STR_METRO_HEAT_CATEGORY, STR_METRO_HEAT_NOTHING_WAITING);
}

static void PrepareStationRating()
{
	static const std::array<int64_t, 5> upper = { 29, 44, 59, 74, 89 };
	ForFilteredStations(true, [&](const Station *st) {
		if (!StationHasTransport(st)) return;
		uint sum = 0, count = 0;
		for (const GoodsEntry &ge : st->goods) {
			if (!ge.HasRating()) continue;
			sum += ge.rating;
			count++;
		}
		if (count == 0) return;
		PaintStation(st, BandOf(static_cast<int64_t>(sum / count) * 100 / 255, upper));
	});
	_heat_colours = _metro_good_colours;
	static const int lows[] = { 0, 30, 45, 60, 75, 90 };
	for (uint i = 0; i < METRO_HEAT_LEVELS; i++) _heat_texts[i] = GetString(STR_METRO_HEAT_LEVEL_PERCENT, lows[i]);
}

static void PrepareProfit()
{
	std::vector<std::pair<const Station *, int64_t>> values;
	std::vector<StationID> route = RouteStations();
	for (const auto &[station, t] : _metro_station_traffic) {
		const Station *st = Station::GetIfValid(StationID{station});
		if (st == nullptr || st->owner != _local_company) continue;
		if (!route.empty() && std::ranges::find(route, st->index) == route.end()) continue;
		int64_t v = StationIncome(t);
		if (v > 0) values.emplace_back(st, v);
	}
	for (const auto &[st, v] : values) _heat_max = std::max(_heat_max, v);
	for (const auto &[st, v] : values) PaintStation(st, LevelOf(v));
	SetLogLegend(true);
}

static void PrepareCoverage()
{
	std::vector<uint8_t> count(Map::Size(), 0);
	ForFilteredStations(true, [&](const Station *st) {
		if (!StationHasTransport(st)) return;
		for (BitmapTileIterator it(st->catchment_tiles); it != INVALID_TILE; ++it) {
			uint8_t &c = count[TileIndex(it).base()];
			if (c < METRO_HEAT_LEVELS) c++;
		}
	});
	for (uint i = 0; i < Map::Size(); i++) _heat_levels[i] = count[i];
	for (uint i = 0; i < METRO_HEAT_LEVELS; i++) {
		_heat_texts[i] = GetString(i + 1 < METRO_HEAT_LEVELS ? STR_METRO_HEAT_LEVEL_STATIONS : STR_METRO_HEAT_LEVEL_STATIONS_MORE, i + 1);
	}
}

static void PrepareJams()
{
	static const std::array<int64_t, 5> upper = { 10, 20, 30, 40, 50 };
	for (TileIndex tile(0); tile < Map::Size(); ++tile) {
		if (!IsTileType(tile, TileType::Road)) continue;
		uint jam = MetroTrafficGetJamPercent(tile);
		if (jam > 0) Paint(tile, BandOf(jam, upper));
	}
	static const int lows[] = { 1, 11, 21, 31, 41, 51 };
	for (uint i = 0; i < METRO_HEAT_LEVELS; i++) _heat_texts[i] = GetString(STR_METRO_HEAT_LEVEL_JAM, lows[i]);
}

static void PrepareMetroLayers()
{
	SetCategories({
		{STR_METRO_HEAT_LAYER_1, PC_YELLOW}, {STR_METRO_HEAT_LAYER_2, PC_ORANGE}, {STR_METRO_HEAT_LAYER_3, PC_RED},
		{STR_METRO_HEAT_INCLINE, PC_GREEN}, {STR_METRO_HEAT_PLATFORM, PC_WHITE}, {STR_METRO_HEAT_ENTRANCE, PC_LIGHT_BLUE},
	});
	for (const auto &[key, ut] : _metro_underground) {
		TileIndex tile{static_cast<uint32_t>(key >> 2)};
		uint layer = key & 3;
		uint8_t level = static_cast<uint8_t>(std::clamp<uint>(layer, 1, 3));
		if (ut.IsIncline()) level = 4;
		if (ut.depot == METRO_UNDERGROUND_PLATFORM) level = 5;
		Paint(tile, level);
	}
	for (const Object *o : Object::Iterate()) {
		if (IsMetroEntranceObject(o->type)) Paint(o->location.tile, 6);
	}
	for (const auto &[tile, entrance] : _metro_extra_entrances) Paint(TileIndex{tile}, 6);
}

static void PrepareVentilation()
{
	SetCategories({
		{STR_METRO_HEAT_VENT_GRID, PC_GREEN}, {STR_METRO_HEAT_VENT_DIESEL, PC_YELLOW}, {STR_METRO_HEAT_VENT_OFF, PC_GREY},
		{STR_METRO_HEAT_DEEP_AIRED, PC_LIGHT_BLUE}, {STR_METRO_HEAT_DEEP_STUFFY, PC_RED},
	});
	/* Coverage of the shafts: a working one wins over one that does not work. */
	static const uint8_t vent_level[] = { 3, 1, 2 }; // state 0 off, 1 grid, 2 diesel
	std::vector<MetroPowerMapItem> items = MetroPowerGetMapItems();
	for (int pass = 0; pass < 2; pass++) {
		for (const MetroPowerMapItem &item : items) {
			if (item.kind != MetroPowerMapKind::Vent) continue;
			bool working = item.state != 0;
			if ((pass == 0) == working) continue; // Not working first, the working ones paint over.
			uint8_t level = vent_level[std::min<uint>(item.state, 2)];
			int cx = TileX(item.tile), cy = TileY(item.tile), r = MetroPowerVentRadius();
			for (int y = std::max(0, cy - r); y <= std::min<int>(Map::MaxY(), cy + r); y++) {
				for (int x = std::max(0, cx - r); x <= std::min<int>(Map::MaxX(), cx + r); x++) {
					uint8_t &l = _heat_levels[TileXY(x, y).base()];
					if (l == 0 || l == 3 || (working && level < l)) l = level;
				}
			}
		}
	}
	/* Platforms of -3: aired or stuffy. */
	for (const auto &[key, ut] : _metro_underground) {
		if ((key & 3) != 3 || ut.depot != METRO_UNDERGROUND_PLATFORM) continue;
		TileIndex tile{static_cast<uint32_t>(key >> 2)};
		bool aired = MetroPowerHasWorkingVentNear(TileArea(tile, 1, 1), MetroPowerVentRadius(), Owner{ut.owner});
		_heat_levels[tile.base()] = aired ? 4 : 5;
	}
}

/** Paint the tiles of a grid building (the whole object). */
static void PaintGridBuilding(TileIndex tile, uint8_t level)
{
	if (IsTileType(tile, TileType::Object)) {
		for (TileIndex t : Object::GetByTile(tile)->location) Paint(t, level);
	} else {
		Paint(tile, level);
	}
}

static void PrepareGrid()
{
	SetCategories({
		{STR_METRO_HEAT_GRID_OFF, PC_ORANGE}, {STR_METRO_HEAT_GRID_FED, PC_GREEN}, {STR_METRO_HEAT_GRID_LINE, PC_WHITE},
		{STR_METRO_HEAT_GRID_PLANT, PC_YELLOW}, {STR_METRO_HEAT_GRID_SUBSTATION, PC_LIGHT_BLUE}, {STR_METRO_HEAT_GRID_VENT, PC_DARK_BLUE},
	});
	std::vector<MetroPowerMapItem> items = MetroPowerGetMapItems();
	/* Radius of the substations first: cut off, then fed over it. */
	for (uint8_t state : {2, 1}) {
		for (const MetroPowerMapItem &item : items) {
			if (item.kind != MetroPowerMapKind::Substation || item.state != state) continue;
			int cx = TileX(item.tile), cy = TileY(item.tile), r = MetroPowerSubstationRadius();
			for (int y = std::max(0, cy - r); y <= std::min<int>(Map::MaxY(), cy + r); y++) {
				for (int x = std::max(0, cx - r); x <= std::min<int>(Map::MaxX(), cx + r); x++) {
					uint8_t &l = _heat_levels[TileXY(x, y).base()];
					if (l <= 2) l = state == 1 ? 2 : std::max<uint8_t>(l, 1);
				}
			}
		}
	}
	for (const MetroPowerMapItem &item : items) {
		switch (item.kind) {
			case MetroPowerMapKind::Line: _heat_levels[item.tile.base()] = 3; break;
			case MetroPowerMapKind::Plant: PaintGridBuilding(item.tile, 4); break;
			case MetroPowerMapKind::Substation: PaintGridBuilding(item.tile, 5); break;
			case MetroPowerMapKind::Vent: PaintGridBuilding(item.tile, 6); break;
		}
	}
}

static void PrepareGridLoad()
{
	static const std::array<int64_t, 5> upper = { 20, 40, 60, 80, 100 };
	std::vector<MetroPowerMapItem> items = MetroPowerGetMapItems();
	for (const MetroPowerMapItem &item : items) {
		uint8_t level = BandOf(item.load_percent, upper);
		if (item.kind == MetroPowerMapKind::Substation) PaintSquare(item.tile, MetroPowerSubstationRadius(), level);
		if (item.kind == MetroPowerMapKind::Line) {
			Paint(item.tile, level);
		} else {
			PaintGridBuilding(item.tile, level);
		}
	}
	static const int lows[] = { 0, 21, 41, 61, 81 };
	for (uint i = 0; i + 1 < METRO_HEAT_LEVELS; i++) _heat_texts[i] = GetString(STR_METRO_HEAT_LEVEL_LOAD, lows[i]);
	_heat_texts[METRO_HEAT_LEVELS - 1] = GetString(STR_METRO_HEAT_CATEGORY, STR_METRO_HEAT_OVERLOAD);
}

static void PrepareElectrification()
{
	SetCategories({
		{STR_METRO_HEAT_RAIL_PLAIN, PC_GREY}, {STR_METRO_HEAT_RAIL_FED, PC_GREEN}, {STR_METRO_HEAT_RAIL_UNFED, PC_RED},
	});
	/* Tiles fed by a powered substation, by owner + 1. */
	std::vector<uint8_t> fed(Map::Size(), 0);
	for (const MetroPowerMapItem &item : MetroPowerGetMapItems()) {
		if (item.kind != MetroPowerMapKind::Substation || item.state != 1) continue;
		int cx = TileX(item.tile), cy = TileY(item.tile), r = MetroPowerSubstationRadius();
		for (int y = std::max(0, cy - r); y <= std::min<int>(Map::MaxY(), cy + r); y++) {
			for (int x = std::max(0, cx - r); x <= std::min<int>(Map::MaxX(), cx + r); x++) fed[TileXY(x, y).base()] = item.owner.base() + 1;
		}
	}
	for (TileIndex tile(0); tile < Map::Size(); ++tile) {
		if (!IsTileType(tile, TileType::Railway)) continue;
		const RailTypeInfo *rti = GetRailTypeInfo(GetRailType(tile));
		if (!rti->flags.Test(RailTypeFlag::Catenary)) {
			_heat_levels[tile.base()] = 1;
		} else {
			_heat_levels[tile.base()] = fed[tile.base()] == GetTileOwner(tile).base() + 1 ? 2 : 3;
		}
	}
}

static void PrepareActiveTowns()
{
	SetCategories({{STR_METRO_HEAT_TOWN_INACTIVE, PC_GREY}, {STR_METRO_HEAT_TOWN_ACTIVE, PC_GREEN}});
	for (const Town *t : Town::Iterate()) PaintTownZone(t, MetroCityIsTownActive(t) ? 2 : 1);
}

static void PrepareTownRating()
{
	static const std::array<int64_t, 5> upper = { -401, -201, 0, 200, 600 };
	for (const Town *t : Town::Iterate()) {
		if (_local_company == CompanyID::Invalid() || !t->have_ratings.Test(_local_company)) continue;
		PaintTownZone(t, BandOf(t->ratings[_local_company], upper));
	}
	_heat_colours = _metro_good_colours;
	static const int lows[] = { -1000, -400, -200, 1, 201, 601 };
	for (uint i = 0; i < METRO_HEAT_LEVELS; i++) _heat_texts[i] = GetString(STR_METRO_HEAT_LEVEL_RATING, lows[i]);
}

static void PrepareDistricts()
{
	SetCategories({
		{STR_NULL, PC_BLACK}, {STR_METRO_HEAT_DISTRICT_BUSINESS, PC_LIGHT_BLUE}, {STR_METRO_HEAT_DISTRICT_RESIDENTIAL, PC_GREEN},
		{STR_NULL, PC_BLACK}, {STR_NULL, PC_BLACK}, {STR_METRO_HEAT_DISTRICT_CENTRE, PC_WHITE},
	});
	for (TileIndex tile(0); tile < Map::Size(); ++tile) {
		if (!IsTileType(tile, TileType::House)) continue;
		const Town *t = Town::GetIfValid(GetTownIndex(tile));
		if (t == nullptr || !MetroCityIsTownActive(t)) continue;
		int type = GetMetroDistrictTypeAt(t, tile);
		if (type == to_underlying(MetroDistrictType::Business)) _heat_levels[tile.base()] = 2;
		if (type == to_underlying(MetroDistrictType::Residential)) _heat_levels[tile.base()] = 3;
	}
	for (const MetroDistrict &d : _metro_districts) Paint(TileIndex{d.tile}, 6);
}

static void PrepareTourists()
{
	std::vector<std::pair<const Town *, int64_t>> values;
	for (const Town *t : Town::Iterate()) {
		const MetroCityTownState *s = MetroCityGetTownState(t);
		if (s != nullptr && s->tourists_last_month > 0) values.emplace_back(t, s->tourists_last_month);
	}
	for (const auto &[t, v] : values) _heat_max = std::max(_heat_max, v);
	for (const auto &[t, v] : values) PaintTownZone(t, LevelOf(v));
	SetLogLegend(false);
	if (_heat_max <= 0) _heat_texts[0] = GetString(STR_METRO_HEAT_CATEGORY, STR_METRO_HEAT_NO_TOURISTS);
}

static void PrepareMigration()
{
	SetCategories({
		{STR_METRO_HEAT_MIGRATION_OUT_MUCH, PC_RED}, {STR_METRO_HEAT_MIGRATION_OUT, PC_ORANGE}, {STR_METRO_HEAT_MIGRATION_NONE, PC_YELLOW},
		{STR_METRO_HEAT_MIGRATION_IN, PC_GREEN}, {STR_METRO_HEAT_MIGRATION_IN_MUCH, PC_LIGHT_BLUE},
	});
	for (const Town *t : Town::Iterate()) {
		if (!MetroCityIsTownActive(t)) continue;
		auto it = _metro_town_life.find(t->index.base());
		if (it == _metro_town_life.end()) {
			PaintTownZone(t, 3);
			continue;
		}
		const MetroTownLifeState &s = it->second;
		/* Last year, or this year while there is no full year yet. */
		int64_t in = s.moved_in_last_year + s.moved_in_year, out = s.moved_out_last_year + s.moved_out_year;
		int64_t net = in - out;
		int64_t permille = net * 1000 / std::max<int64_t>(t->cache.population, 1);
		uint8_t level = net == 0 ? 3 : (permille <= -10 ? 1 : (net < 0 ? 2 : (permille >= 10 ? 5 : 4)));
		PaintTownZone(t, level);
	}
}

/** Rebuild the tile levels if the data, the mode or the filters changed. */
void MetroHeatPrepare()
{
	bool stale = IsLiveMode(_heat_mode) && _tick_counter - _heat_prepared_tick >= DAY_TICKS;
	if (!_heat_dirty && !stale && _heat_levels.size() == Map::Size()) return;
	_heat_dirty = false;
	_heat_prepared_tick = _tick_counter;
	_heat_levels.assign(Map::Size(), 0);
	_heat_max = 0;
	_heat_colours = _metro_heat_colours;
	for (std::string &s : _heat_texts) s.clear();
	switch (_heat_mode) {
		case MetroMapMode::PassengerFlow:
		case MetroMapMode::FreightFlow: PrepareFlow(); break;
		case MetroMapMode::StationLoad: PrepareStationLoad(); break;
		case MetroMapMode::StationRating: PrepareStationRating(); break;
		case MetroMapMode::Jams: PrepareJams(); break;
		case MetroMapMode::Profit: PrepareProfit(); break;
		case MetroMapMode::Coverage: PrepareCoverage(); break;
		case MetroMapMode::MetroLayers: PrepareMetroLayers(); break;
		case MetroMapMode::Ventilation: PrepareVentilation(); break;
		case MetroMapMode::Grid: PrepareGrid(); break;
		case MetroMapMode::GridLoad: PrepareGridLoad(); break;
		case MetroMapMode::Electrification: PrepareElectrification(); break;
		case MetroMapMode::ActiveTowns: PrepareActiveTowns(); break;
		case MetroMapMode::TownRating: PrepareTownRating(); break;
		case MetroMapMode::Districts: PrepareDistricts(); break;
		case MetroMapMode::Tourists: PrepareTourists(); break;
		case MetroMapMode::Migration: PrepareMigration(); break;
		default: NOT_REACHED();
	}
}

uint8_t MetroHeatLevel(TileIndex tile)
{
	return tile.base() < _heat_levels.size() ? _heat_levels[tile.base()] : 0;
}

PixelColour MetroHeatLevelColour(uint level)
{
	return level >= 1 && level <= METRO_HEAT_LEVELS ? _heat_colours[level - 1] : PC_BLACK;
}

/* ---------------------------------------------------------------------------------------------- */
/* Legend                                                                                         */
/* ---------------------------------------------------------------------------------------------- */

uint MetroHeatLegendRows()
{
	return METRO_HEAT_FILTER_ROWS + METRO_HEAT_LEVELS;
}

static const StringID _heat_transport_names[] = { STR_METRO_HEAT_ALL_TRANSPORT, STR_METRO_HEAT_TRAINS, STR_METRO_HEAT_ROAD, STR_METRO_HEAT_SHIPS, STR_METRO_HEAT_AIRCRAFT };
static const StringID _heat_period_names[] = { STR_METRO_HEAT_MONTH, STR_METRO_HEAT_YEAR };

std::string MetroHeatLegendText(uint row)
{
	switch (row) {
		case 0: return GetString(STR_METRO_MAP_MODE_ROW, _heat_mode_names[to_underlying(_heat_mode)]);
		case 1:
			if (!IsStationMode(_heat_mode)) return GetString(STR_METRO_HEAT_FILTER_OFF, _heat_transport_names[_heat_transport]);
			return GetString(STR_METRO_HEAT_FILTER, _heat_transport_names[_heat_transport]);
		case 2:
			if (Vehicle::GetIfValid(_heat_route) == nullptr) return GetString(IsStationMode(_heat_mode) ? STR_METRO_HEAT_ROUTE_ALL : STR_METRO_HEAT_ROUTE_ALL_OFF);
			return GetString(IsStationMode(_heat_mode) ? STR_METRO_HEAT_ROUTE_VEHICLE : STR_METRO_HEAT_ROUTE_VEHICLE_OFF, _heat_route);
		case 3:
			if (!HasPeriod(_heat_mode)) return GetString(STR_METRO_HEAT_FILTER_OFF, _heat_period_names[_heat_period]);
			return GetString(STR_METRO_HEAT_FILTER, _heat_period_names[_heat_period]);
		default: {
			uint level = row - METRO_HEAT_FILTER_ROWS;
			return level < METRO_HEAT_LEVELS ? _heat_texts[level] : std::string{};
		}
	}
}

PixelColour MetroHeatLegendColour(uint row)
{
	if (row < METRO_HEAT_FILTER_ROWS) return PC_WHITE;
	uint level = row - METRO_HEAT_FILTER_ROWS;
	return level < METRO_HEAT_LEVELS && !_heat_texts[level].empty() ? _heat_colours[level] : PC_BLACK;
}

void MetroHeatLegendClick(uint row, bool back)
{
	switch (row) {
		case 0: {
			uint n = to_underlying(MetroMapMode::End);
			_heat_mode = static_cast<MetroMapMode>((to_underlying(_heat_mode) + (back ? n - 1 : 1)) % n);
			break;
		}
		case 1: _heat_transport = (_heat_transport + 1) % std::size(_heat_transport_names); break;
		case 2: _heat_route = VehicleID::Invalid(); break;
		case 3: _heat_period = (_heat_period + 1) % std::size(_heat_period_names); break;
		default: return;
	}
	_heat_dirty = true;
}

/** Show the stations of the orders of the vehicle on the heat map (from its orders window). */
void MetroHeatSetRoute(VehicleID vehicle)
{
	_heat_route = vehicle;
	if (!IsStationMode(_heat_mode)) _heat_mode = MetroMapMode::PassengerFlow;
	_heat_dirty = true;
	MetroShowHeatSmallMap();
}

bool MetroHeatShowsLinks()
{
	return IsFlowMode(_heat_mode);
}

/** Cargoes of the link lines of the flow mode. */
CargoTypes MetroHeatOverlayCargoMask()
{
	CargoTypes mask{};
	for (const CargoSpec *cs : CargoSpec::Iterate()) {
		bool pax = cs->classes.Test(CargoClass::Passengers);
		if ((_heat_mode == MetroMapMode::PassengerFlow) == pax) mask.Set(cs->Index());
	}
	return mask;
}

/** The station with the most traffic in the period (INVALID_TILE if none). */
TileIndex MetroHeatBusiestStation()
{
	TileIndex best = INVALID_TILE;
	uint32_t best_value = 0;
	for (const auto &[station, t] : _metro_station_traffic) {
		const Station *st = Station::GetIfValid(StationID{station});
		uint32_t v = StationValue(t);
		if (st != nullptr && v > best_value) {
			best_value = v;
			best = st->xy;
		}
	}
	return best;
}

/** Stations with cargo transferred in the period (the transfer icons). */
std::vector<TileIndex> MetroHeatTransferStations()
{
	std::vector<TileIndex> tiles;
	std::vector<StationID> route = RouteStations();
	for (const auto &[station, t] : _metro_station_traffic) {
		const Station *st = Station::GetIfValid(StationID{station});
		if (st == nullptr || StationTransfers(t) == 0) continue;
		if (!route.empty() && std::ranges::find(route, st->index) == route.end()) continue;
		tiles.push_back(st->xy);
	}
	return tiles;
}

/**
 * Console "heat": the stations with their traffic;
 * "heat filter <transport 0-4> <period 0-1>"; "heat mode <0-16>": switch the mode and count the tinted tiles per level;
 * "heat route <vehicle>|off".
 */
void MetroHeatConsole(const std::vector<std::string_view> &args)
{
	if (args.size() >= 3 && args[0] == "filter") {
		_heat_transport = static_cast<uint8_t>(std::min<uint>(ParseInteger<uint>(args[1]).value_or(0), 4));
		_heat_period = static_cast<uint8_t>(std::min<uint>(ParseInteger<uint>(args[2]).value_or(0), 1));
		_heat_dirty = true;
		SetWindowClassesDirty(WindowClass::SmallMap);
	}
	if (args.size() >= 2 && args[0] == "route") {
		_heat_route = args[1] == "off" ? VehicleID::Invalid() : VehicleID(ParseInteger<uint>(args[1]).value_or(VehicleID::Invalid().base()));
		_heat_dirty = true;
		SetWindowClassesDirty(WindowClass::SmallMap);
	}
	if (args.size() >= 2 && args[0] == "mode") {
		_heat_mode = static_cast<MetroMapMode>(std::min<uint>(ParseInteger<uint>(args[1]).value_or(0), to_underlying(MetroMapMode::End) - 1));
		_heat_dirty = true;
		SetWindowClassesDirty(WindowClass::SmallMap);
		MetroHeatPrepare();
		std::array<uint, METRO_HEAT_LEVELS + 1> per_level{};
		for (uint8_t l : _heat_levels) per_level[std::min<uint>(l, METRO_HEAT_LEVELS)]++;
		IConsolePrint(CC_INFO, "map mode {} '{}': route {} stations", to_underlying(_heat_mode), GetString(_heat_mode_names[to_underlying(_heat_mode)]), RouteStations().size());
		for (uint level = 1; level <= METRO_HEAT_LEVELS; level++) {
			IConsolePrint(CC_DEFAULT, "  level {}: {} tiles, legend '{}'", level, per_level[level], _heat_texts[level - 1]);
		}
		return;
	}
	MetroHeatPrepare();
	uint tiles = 0;
	for (uint8_t l : _heat_levels) tiles += l != 0;
	IConsolePrint(CC_INFO, "heat map: mode {} transport {} period {} route {}, largest value {}, tinted tiles {}", to_underlying(_heat_mode), _heat_transport, _heat_period, _heat_route.base(), _heat_max, tiles);
	for (const auto &[station, t] : _metro_station_traffic) {
		uint32_t month = 0;
		for (uint32_t v : t.month) month += v;
		IConsolePrint(CC_DEFAULT, "station {} '{}': this month {}, value {}, level {}, transfers {}, income last month {}", station, GetString(STR_STATION_NAME, station),
				month, StationValue(t), LevelOf(StationValue(t)), StationTransfers(t), StationIncome(t));
	}
}
