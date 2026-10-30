/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_city.h Metro mod: active towns, town services, tourists and stadium matches. */

#ifndef METRO_CITY_H
#define METRO_CITY_H

#include "cargo_type.h"
#include "economy_type.h"
#include "gfx_type.h"
#include "object_type.h"
#include "station_type.h"
#include "strings_type.h"
#include "tile_type.h"
#include "town_type.h"

#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct Town;
struct TileInfo;
struct Station;

/** Services the player can build in an active town. Data: table/metro_city_tables.h. */
enum class MetroServiceType : uint8_t {
	School,
	Hospital,
	University,
	End,
};

static constexpr uint METRO_SERVICE_COUNT = static_cast<uint>(MetroServiceType::End);

/** Importance of a stadium match. */
enum class MetroMatchLevel : uint8_t {
	Local,    ///< Fans of the own town.
	Regional, ///< Fans of the neighbour towns.
	Final,    ///< Fans of all towns connected by the player's network.
	End,
};

/** Mod state of one town (saved in the MTCT chunk). */
struct MetroCityTownState {
	uint16_t town = 0;                  ///< Town index.
	bool active = false;                ///< The town is active: the player has moved cargo at a station in its local authority zone.
	int32_t active_since = 0;           ///< Calendar date of the activation.
	uint32_t tourists_month = 0;        ///< Tourists delivered this month.
	uint32_t tourists_last_month = 0;   ///< Tourists delivered last month.
	int64_t tourist_income_month = 0;   ///< Extra income from tourists this month.
	int64_t tourist_income_last_month = 0; ///< Extra income from tourists last month.
	int32_t next_match = 0;             ///< Calendar date of the next match, 0 if none is planned.
	uint8_t match_level = 0;            ///< #MetroMatchLevel of the next match.
	bool match_announced = false;       ///< The next match has been announced in the news.
	int32_t match_day = 0;              ///< Calendar date of the match being played (bonus day), 0 if none.
	uint8_t match_day_level = 0;        ///< #MetroMatchLevel of the match being played.
};

extern std::map<uint16_t, MetroCityTownState> _metro_city_towns;
extern bool _metro_show_landmarks;

void MetroCityReset();

bool MetroCityIsTownActive(const Town *t);
bool MetroCityIsTownActive(TownID town);
const MetroCityTownState *MetroCityGetTownState(const Town *t);
void MetroCityOnTownDeleted(TownID town);
void MetroCityOnCargoMoved(const Station *st);

/* Services. */
bool IsMetroServiceObject(ObjectType type);
MetroServiceType GetMetroServiceTypeOfObject(ObjectType type);
StringID GetMetroServiceName(MetroServiceType type);
Money GetMetroServiceCost(MetroServiceType type);
uint32_t GetMetroServiceMinPopulation(MetroServiceType type);
uint CountMetroServices(const Town *t, MetroServiceType type);
bool MetroCityGetServiceInfo(TileIndex tile, MetroServiceType &type, TownID &town, int32_t &build_date);
std::vector<std::string> MetroCityGetServiceLines(TileIndex tile);
bool MetroCityClickTile(TileIndex tile);
bool MetroCityAddAcceptedCargo(TileIndex tile, CargoArray &acceptance, CargoTypes &always_accepted);
bool MetroCityAddProducedCargo(TileIndex tile, CargoArray &produced);
uint MetroCityAdjustGrowthRate(const Town *t, uint rate);

/* Drawing. */
void MetroCityDrawTile(const TileInfo *ti);
PaletteID MetroCityAdjustHousePalette(TileIndex tile, PaletteID pal);

/* Tourists and matches. */
Money MetroCityDeliveryBonus(const Station *st, CargoType cargo, uint amount, Money profit);

/* Town window. */
std::vector<std::string> MetroCityGetTownViewLines(const Town *t);
void ShowMetroTownServicesWindow(TownID town);
void ShowMetroServiceWindow(TileIndex tile);
void MetroCityToggleLandmarks();

bool ConMetroCity(std::span<std::string_view> argv);

#endif /* METRO_CITY_H */
