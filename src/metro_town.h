/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_town.h Metro mod: town life of active towns — districts, commuting and migration. */

#ifndef METRO_TOWN_H
#define METRO_TOWN_H

#include "tile_type.h"
#include "town_type.h"

#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct Town;
struct HouseSpec;

/** Type of a district. Data: table/metro_town_tables.h. */
enum class MetroDistrictType : uint8_t {
	Business,
	Residential,
	End,
};

/** A district: houses of the town nearest to its centre belong to it (MTDS chunk). */
struct MetroDistrict {
	uint16_t town = 0;
	uint32_t tile = 0;  ///< Centre.
	uint8_t type = 0;   ///< #MetroDistrictType.
};

/** Town life state of a town (MTTL chunk). */
struct MetroTownLifeState {
	uint16_t town = 0;
	uint32_t commuters_month = 0;          ///< Commuters sent to the player's stations this month.
	uint32_t commuters_last_month = 0;
	uint32_t commuters_out_month = 0;      ///< Of them: working in a neighbour town (not enough jobs at home).
	uint32_t commuters_out_last_month = 0;
	uint8_t attractiveness = 0;            ///< 0..100, computed every 5 months.
	uint32_t moved_in_year = 0;            ///< People who moved here this year.
	uint32_t moved_out_year = 0;
	uint32_t moved_in_last_year = 0;
	uint32_t moved_out_last_year = 0;
	int8_t growth_effect = 0;              ///< Effect of the last migration on growth: -1 slower (people leave), 1 faster (people come).
};

extern std::vector<MetroDistrict> _metro_districts;
extern std::map<uint16_t, MetroTownLifeState> _metro_town_life;

void MetroTownReset();
void MetroTownOnTownDeleted(TownID town);

int GetMetroDistrictTypeAt(const Town *t, TileIndex tile);
uint MetroTownAdjustHouseProbability(const Town *t, TileIndex tile, const HouseSpec &hs, uint probability);
uint MetroTownAdjustGrowthRate(const Town *t, uint rate);
std::vector<std::string> MetroTownGetViewLines(const Town *t);

/** Residents and jobs of a district type in a town, for the window. */
struct MetroDistrictStats {
	uint districts = 0;
	uint32_t population = 0;
	uint32_t houses = 0;
};
std::vector<MetroDistrictStats> MetroTownGetDistrictStats(const Town *t);
uint32_t MetroTownGetWorkers(const Town *t);
uint32_t MetroTownGetJobs(const Town *t);
uint MetroTownGetMapRadius(const Town *t);
void ShowMetroDistrictsWindow(TownID town);

bool ConMetroTown(std::span<std::string_view> argv);

#endif /* METRO_TOWN_H */
