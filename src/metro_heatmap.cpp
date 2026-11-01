/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_heatmap.cpp Metro mod: heat map (stage 9, decisions of the user).
 *
 * Every station counts the cargo its vehicles load and unload, by vehicle type and passengers / other cargo, and the
 * cargo transferred there; 12 monthly totals are kept (#_metro_station_traffic, saved). The heat map mode of the small
 * map tints the catchment of each station from blue to red by its traffic in the chosen period (the larger value where
 * catchments overlap), draws the vanilla link lines of the chosen cargo coloured by volume and marks stations with
 * transfers. Filters (legend rows under the map): vehicle type, passengers or cargo, last month or last 12 months.
 */

#include "stdafx.h"
#include "metro_heatmap.h"
#include "station_base.h"
#include "cargotype.h"
#include "bitmap_type.h"
#include "console_func.h"
#include "console_type.h"
#include "strings_func.h"
#include "palette_func.h"
#include "window_func.h"
#include "timer/timer.h"
#include "timer/timer_game_calendar.h"
#include "core/string_consumer.hpp"
#include "table/strings.h"

#include <cmath>

#include "safeguards.h"

std::map<uint16_t, MetroStationTraffic> _metro_station_traffic;

/** Colours of the levels 1..6, from blue to red. */
static const PixelColour _metro_heat_colours[] = { PC_DARK_BLUE, PC_LIGHT_BLUE, PC_GREEN, PC_YELLOW, PC_ORANGE, PC_RED };
static constexpr uint METRO_HEAT_LEVELS = static_cast<uint>(std::size(_metro_heat_colours));

/* Filters of the view (not saved). */
static uint8_t _heat_transport = 0; ///< 0 = all, 1..4 = train, road vehicle, ship, aircraft.
static uint8_t _heat_cargo = 0;     ///< 0 = all, 1 = passengers, 2 = other cargo.
static uint8_t _heat_period = 0;    ///< 0 = last month, 1 = last 12 months.

static std::vector<uint8_t> _heat_levels;   ///< Level 0..6 per tile of the last preparation.
static bool _heat_dirty = true;
static uint32_t _heat_max = 0;              ///< Largest station value of the last preparation.

void MetroHeatReset()
{
	_metro_station_traffic.clear();
	_heat_levels.clear();
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

/** A new month: the current totals become last month's. */
static void RotateTraffic()
{
	for (auto &[station, t] : _metro_station_traffic) {
		for (uint m = METRO_HEAT_MONTHS - 1; m > 0; m--) {
			t.history[m] = t.history[m - 1];
			t.transfers[m] = t.transfers[m - 1];
		}
		t.history[0] = t.month;
		t.transfers[0] = t.transfers_month;
		t.month.fill(0);
		t.transfers_month = 0;
	}
	_heat_dirty = true;
	SetWindowClassesDirty(WindowClass::SmallMap);
}

static const IntervalTimer<TimerGameCalendar> _metro_heat_monthly({TimerGameCalendar::Trigger::Month, TimerGameCalendar::Priority::None}, [](auto) {
	RotateTraffic();
});

/** Whether the category passes the filters. */
static bool CategoryShown(uint c)
{
	if (_heat_transport != 0 && c / 2 != _heat_transport - 1u) return false;
	if (_heat_cargo == 1 && (c % 2) != 0) return false;
	if (_heat_cargo == 2 && (c % 2) != 1) return false;
	return true;
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

static uint32_t StationTransfers(const MetroStationTraffic &t)
{
	uint32_t sum = 0;
	uint months = _heat_period == 0 ? 1 : METRO_HEAT_MONTHS;
	for (uint m = 0; m < months; m++) sum += t.transfers[m];
	return sum;
}

/** Level 1..6 of a value (logarithmic up to the largest station). */
static uint8_t LevelOf(uint32_t value)
{
	if (value == 0 || _heat_max == 0) return 0;
	if (_heat_max <= 1) return METRO_HEAT_LEVELS;
	double f = std::log(static_cast<double>(value)) / std::log(static_cast<double>(_heat_max));
	uint level = 1 + static_cast<uint>(f * (METRO_HEAT_LEVELS - 1) + 0.5);
	return static_cast<uint8_t>(std::clamp<uint>(level, 1, METRO_HEAT_LEVELS));
}

/** Lowest value of a level (for the legend). */
static uint32_t LevelThreshold(uint level)
{
	if (_heat_max <= 1 || level <= 1) return _heat_max == 0 ? 0 : 1;
	double f = (level - 1.5) / (METRO_HEAT_LEVELS - 1);
	return static_cast<uint32_t>(std::ceil(std::pow(static_cast<double>(_heat_max), f)));
}

/** Rebuild the tile levels if the traffic or the filters changed. */
void MetroHeatPrepare()
{
	if (!_heat_dirty && _heat_levels.size() == Map::Size()) return;
	_heat_dirty = false;
	_heat_levels.assign(Map::Size(), 0);
	_heat_max = 0;
	for (const auto &[station, t] : _metro_station_traffic) _heat_max = std::max(_heat_max, StationValue(t));
	if (_heat_max == 0) return;
	for (const auto &[station, t] : _metro_station_traffic) {
		const Station *st = Station::GetIfValid(StationID{station});
		if (st == nullptr) continue;
		uint8_t level = LevelOf(StationValue(t));
		if (level == 0) continue;
		bool any = false;
		for (BitmapTileIterator it(st->catchment_tiles); it != INVALID_TILE; ++it) {
			uint8_t &l = _heat_levels[TileIndex(it).base()];
			l = std::max(l, level);
			any = true;
		}
		if (!any) {
			uint8_t &l = _heat_levels[st->xy.base()];
			l = std::max(l, level);
		}
	}
}

uint8_t MetroHeatLevel(TileIndex tile)
{
	return tile.base() < _heat_levels.size() ? _heat_levels[tile.base()] : 0;
}

PixelColour MetroHeatLevelColour(uint level)
{
	return level >= 1 && level <= METRO_HEAT_LEVELS ? _metro_heat_colours[level - 1] : PC_BLACK;
}

/* Legend: 3 filter rows, then one row per level. */
uint MetroHeatLegendRows()
{
	return 3 + METRO_HEAT_LEVELS;
}

static const StringID _heat_transport_names[] = { STR_METRO_HEAT_ALL_TRANSPORT, STR_METRO_HEAT_TRAINS, STR_METRO_HEAT_ROAD, STR_METRO_HEAT_SHIPS, STR_METRO_HEAT_AIRCRAFT };
static const StringID _heat_cargo_names[] = { STR_METRO_HEAT_ALL_CARGO, STR_METRO_HEAT_PASSENGERS, STR_METRO_HEAT_FREIGHT };
static const StringID _heat_period_names[] = { STR_METRO_HEAT_MONTH, STR_METRO_HEAT_YEAR };

std::string MetroHeatLegendText(uint row)
{
	switch (row) {
		case 0: return GetString(STR_METRO_HEAT_FILTER, _heat_transport_names[_heat_transport]);
		case 1: return GetString(STR_METRO_HEAT_FILTER, _heat_cargo_names[_heat_cargo]);
		case 2: return GetString(STR_METRO_HEAT_FILTER, _heat_period_names[_heat_period]);
		default: {
			uint level = row - 2;
			if (_heat_max == 0) return level == 1 ? GetString(STR_METRO_HEAT_NO_TRAFFIC) : std::string{};
			return GetString(STR_METRO_HEAT_LEVEL, LevelThreshold(level));
		}
	}
}

PixelColour MetroHeatLegendColour(uint row)
{
	return row < 3 ? PC_WHITE : MetroHeatLevelColour(row - 2);
}

void MetroHeatLegendClick(uint row)
{
	switch (row) {
		case 0: _heat_transport = (_heat_transport + 1) % std::size(_heat_transport_names); break;
		case 1: _heat_cargo = (_heat_cargo + 1) % std::size(_heat_cargo_names); break;
		case 2: _heat_period = (_heat_period + 1) % std::size(_heat_period_names); break;
		default: return;
	}
	_heat_dirty = true;
}

/** Cargoes of the link lines for the cargo filter. */
CargoTypes MetroHeatOverlayCargoMask()
{
	CargoTypes mask{};
	for (const CargoSpec *cs : CargoSpec::Iterate()) {
		bool pax = cs->classes.Test(CargoClass::Passengers);
		if (_heat_cargo == 0 || (_heat_cargo == 1) == pax) mask.Set(cs->Index());
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
	for (const auto &[station, t] : _metro_station_traffic) {
		const Station *st = Station::GetIfValid(StationID{station});
		if (st != nullptr && StationTransfers(t) > 0) tiles.push_back(st->xy);
	}
	return tiles;
}

/** Console "heat": the stations with their traffic; "heat filter <transport 0-4> <cargo 0-2> <period 0-1>". */
void MetroHeatConsole(const std::vector<std::string_view> &args)
{
	if (args.size() >= 4 && args[0] == "filter") {
		_heat_transport = static_cast<uint8_t>(std::min<uint>(ParseInteger<uint>(args[1]).value_or(0), 4));
		_heat_cargo = static_cast<uint8_t>(std::min<uint>(ParseInteger<uint>(args[2]).value_or(0), 2));
		_heat_period = static_cast<uint8_t>(std::min<uint>(ParseInteger<uint>(args[3]).value_or(0), 1));
		_heat_dirty = true;
		SetWindowClassesDirty(WindowClass::SmallMap);
	}
	MetroHeatPrepare();
	uint tiles = 0;
	for (uint8_t l : _heat_levels) tiles += l != 0;
	IConsolePrint(CC_INFO, "heat map: filter transport {} cargo {} period {}, largest station {}, tinted tiles {}", _heat_transport, _heat_cargo, _heat_period, _heat_max, tiles);
	for (const auto &[station, t] : _metro_station_traffic) {
		IConsolePrint(CC_DEFAULT, "station {} '{}': this month {}, value {}, level {}, transfers {}", station, GetString(STR_STATION_NAME, station),
				t.month[0] + t.month[1] + t.month[2] + t.month[3] + t.month[4] + t.month[5] + t.month[6] + t.month[7], StationValue(t), LevelOf(StationValue(t)), StationTransfers(t));
	}
}
