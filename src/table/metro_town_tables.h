/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_town_tables.h Data tables of the metro mod town life: districts, commuting and migration. */

#ifndef METRO_TOWN_TABLES_H
#define METRO_TOWN_TABLES_H

#include "../metro_town.h"
#include "../palette_func.h"
#include "table/strings.h"

/** Description of a district type. */
struct MetroDistrictSpec {
	StringID name;        ///< Name in the town window.
	PixelColour colour;   ///< Colour of its houses on the district map.
};

/** District types, indexed by #MetroDistrictType. */
static const MetroDistrictSpec _metro_district_specs[] = {
	{ STR_METRO_TOWN_DISTRICT_BUSINESS,    PC_LIGHT_BLUE },
	{ STR_METRO_TOWN_DISTRICT_RESIDENTIAL, PC_GREEN },
};
static_assert(std::size(_metro_district_specs) == to_underlying(MetroDistrictType::End));

/* Districts. */
static constexpr uint32_t METRO_DISTRICT_POPULATION_STEP = 1500; ///< One more district per this many residents (beyond the first two).
static constexpr uint METRO_DISTRICT_MAX = 8;                    ///< Most districts of a town.
static constexpr uint METRO_DISTRICT_NEW_CHANCE = 6;             ///< A due new district appears with a chance of 1 in this per month (rarely).
static constexpr uint METRO_DISTRICT_MIN_DISTANCE = 4;           ///< Centres of two districts are at least this many tiles apart.
static constexpr uint METRO_DISTRICT_MAP_MIN_RADIUS = 10;        ///< Smallest radius of the district map in the town window.

/* Houses: business buildings and high-rises grow more often in business districts and near metro stations. */
static constexpr uint8_t METRO_BUSINESS_HOUSE_POPULATION = 80;      ///< Houses with at least this many residents are business buildings or high-rises.
static constexpr uint METRO_BUSINESS_IN_BUSINESS_PERCENT = 300;     ///< Chance of a business house in a business district, in percent of the normal.
static constexpr uint METRO_BUSINESS_IN_RESIDENTIAL_PERCENT = 50;   ///< The same in a residential district.
static constexpr uint METRO_BUSINESS_NEAR_STATION_PERCENT = 300;    ///< The same near a metro station (until stage 8: a rail station of a human player).
static constexpr uint METRO_BUSINESS_STATION_RADIUS = 6;            ///< "Near a station": within this many tiles.

/* Commuting: no rush hour, the same number every day. */
static constexpr uint METRO_COMMUTE_WORKER_PERCENT = 40;   ///< Residents of residential districts who work.
static constexpr uint METRO_COMMUTE_JOB_PERCENT = 60;      ///< Jobs per resident of business districts.
static constexpr uint METRO_COMMUTE_DAILY_PERMILLE = 20;   ///< Commuters who travel by the player's transport each day (each way), per 1000.
static constexpr uint METRO_COMMUTE_SPREAD_HOUSES = 4;     ///< Houses commuters start from per day and direction.

/* Migration: every 5 months people move to better neighbour active towns. */
static constexpr uint METRO_MIGRATION_PERIOD_MONTHS = 5;
static constexpr uint METRO_MIGRATION_REGION_TILES = 100;      ///< Neighbour towns are within this distance.
static constexpr uint METRO_MIGRATION_MIN_GAP = 10;            ///< A town must be this many points more attractive to draw people.
static constexpr uint METRO_MIGRATION_PERMILLE_PER_POINT = 1;  ///< Movers per 1000 residents and point of attractiveness gap.
static constexpr uint METRO_MIGRATION_MAX_PERCENT = 3;         ///< Most residents moving away at once.
static constexpr uint METRO_MIGRATION_OUTFLOW_GROWTH_PERCENT = 130; ///< Growth interval of a town losing people, in percent (bigger is slower).
static constexpr uint METRO_MIGRATION_INFLOW_GROWTH_PERCENT = 85;   ///< Growth interval of a town gaining people.

/** Weights of the attractiveness (sum 100). */
static constexpr uint METRO_ATTRACT_TRANSPORT = 40; ///< Share of passengers carried last month.
static constexpr uint METRO_ATTRACT_SERVICE = 30;   ///< Passenger rating of the player's stations.
static constexpr uint METRO_ATTRACT_SERVICES = 15;  ///< Town services built out of those the town is big enough for.
static constexpr uint METRO_ATTRACT_RATING = 15;    ///< Local authority rating of the player.

#endif /* METRO_TOWN_TABLES_H */
