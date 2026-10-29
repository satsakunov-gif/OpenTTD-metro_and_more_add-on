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
#include "company_base.h"
#include "company_func.h"
#include "console_func.h"
#include "console_type.h"
#include "settings_type.h"
#include "station_base.h"
#include "strings_func.h"
#include "town.h"
#include "window_func.h"
#include "timer/timer.h"
#include "timer/timer_game_calendar.h"
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

static const IntervalTimer<TimerGameCalendar> _metro_city_daily({TimerGameCalendar::Trigger::Day, TimerGameCalendar::Priority::None}, [](auto) {
	UpdateActiveTowns();
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
	return lines;
}

/* ---------------------------------------------------------------------------------------------- */
/* Console                                                                                        */
/* ---------------------------------------------------------------------------------------------- */

bool ConMetroCity(std::span<std::string_view> argv)
{
	if (argv.empty()) {
		IConsolePrint(CC_HELP, "Metro mod towns. Usage: 'metro_city towns'.");
		return true;
	}
	for (const Town *t : Town::Iterate()) {
		const MetroCityTownState *state = MetroCityGetTownState(t);
		if (state == nullptr || !state->active) continue;
		IConsolePrint(CC_DEFAULT, "{}: {} (population {}), active", t->index, GetString(STR_TOWN_NAME, t->index), t->cache.population);
	}
	return true;
}
