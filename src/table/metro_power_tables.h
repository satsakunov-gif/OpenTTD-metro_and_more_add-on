/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_power_tables.h Data tables of the metro mod power grid: plants, substations, vent shafts and power lines. */

#ifndef METRO_POWER_TABLES_H
#define METRO_POWER_TABLES_H

#include "../metro_power.h"
#include "../object_type.h"
#include "sprites.h"
#include "table/strings.h"

/** Description of a building of the power grid. Power is in kW. */
struct MetroPowerBuildingSpec {
	StringID name;            ///< Name of the building.
	StringID effect;          ///< What the building does, shown in its window.
	ObjectType object;        ///< Object type on the map.
	MetroPowerRole role;      ///< What the building is in the grid.
	int32_t intro_year;       ///< First calendar year it can be built.
	uint16_t cost_factor;     ///< Build cost in 1/256 of the town action base price (statue = 35).
	uint8_t upkeep_percent;   ///< Yearly upkeep in percent of the build cost, paid monthly.
	uint32_t power_kw;        ///< Plant: nominal output. Vent shaft: consumption.
	CargoLabel fuel;          ///< Plant: fuel delivered by trains or trucks, or CT_INVALID for none.
	uint16_t fuel_per_month;  ///< Plant: fuel burnt in a month at full output.
};

/** Buildings, indexed by #MetroPowerBuilding. */
static const MetroPowerBuildingSpec _metro_power_building_specs[] = {
	{ STR_METRO_POWER_SUBSTATION, STR_METRO_POWER_EFFECT_SUBSTATION, OBJECT_METRO_SUBSTATION,   MetroPowerRole::Substation, 1900,   6,  4,      0, CT_INVALID,   0 },
	{ STR_METRO_POWER_GAS,        STR_METRO_POWER_EFFECT_GAS,        OBJECT_METRO_GAS_PLANT,     MetroPowerRole::Plant,      1970,  60,  5,  30000, CT_OIL,      150 },
	{ STR_METRO_POWER_NUCLEAR,    STR_METRO_POWER_EFFECT_NUCLEAR,    OBJECT_METRO_NUCLEAR_PLANT, MetroPowerRole::Plant,      1980, 160, 12, 100000, CT_INVALID,   0 },
	{ STR_METRO_POWER_SOLAR,      STR_METRO_POWER_EFFECT_SOLAR,      OBJECT_METRO_SOLAR_PLANT,   MetroPowerRole::Plant,      2000,  40,  2,   4000, CT_INVALID,   0 },
	{ STR_METRO_POWER_WIND,       STR_METRO_POWER_EFFECT_WIND,       OBJECT_METRO_WIND_PLANT,    MetroPowerRole::Plant,      2000,  12,  3,   2000, CT_INVALID,   0 },
	{ STR_METRO_POWER_VENT,       STR_METRO_POWER_EFFECT_VENT,       OBJECT_METRO_VENT_SHAFT,    MetroPowerRole::Vent,       1900,   8,  4,    400, CT_INVALID,   0 },
};
static_assert(std::size(_metro_power_building_specs) == METRO_POWER_BUILDING_COUNT);

/* The vanilla coal power station (industry) feeds the grid when a power line or grid building of the company touches it. */
static constexpr uint32_t METRO_POWER_COAL_STATION_KW = 20000;    ///< Output of a vanilla power station with coal in stock.
static constexpr uint16_t METRO_POWER_COAL_PER_MONTH = 120;       ///< Tonnes of coal it burns in a month at full output.

static constexpr uint32_t METRO_POWER_FUEL_STOCK_MAX = 5000;      ///< Most fuel a plant keeps in stock.
static constexpr uint METRO_POWER_NO_FUEL_FADE_DAYS = 30;         ///< Without fuel the output falls to zero in this many days.

static constexpr uint METRO_POWER_SUBSTATION_RADIUS = 8;          ///< A substation feeds tracks and vent shafts this many tiles around it (square).
static constexpr uint METRO_POWER_VENT_RADIUS = 8;                ///< A vent shaft ventilates the deep metro this many tiles around it (stage 8).
static constexpr int32_t METRO_POWER_DIESEL_YEAR = 1970;          ///< From this year vent shafts have an emergency diesel generator.
static constexpr uint8_t METRO_POWER_DIESEL_COST_SHIFT = 13;      ///< Daily cost of a running diesel generator: town action base price >> this.

static constexpr uint8_t METRO_POWER_LINE_COST_FACTOR = 3;        ///< Build cost of a power line tile, in rail build prices.
static constexpr uint8_t METRO_POWER_LINE_REMOVE_FACTOR = 1;      ///< Removal cost of a power line tile, in rail build prices.
static constexpr uint8_t METRO_POWER_LINE_UPKEEP_SHIFT = 5;       ///< Monthly upkeep of a power line tile: rail build price >> this.

static constexpr uint METRO_POWER_UPDATE_TICKS = 32;              ///< Supply, demand and the power of trains are updated this often.
static constexpr uint METRO_POWER_HP_TO_KW_PERMILLE = 746;        ///< Engine power in hp to kW, in 1/1000.

/* Temporary vanilla sprites of the grid buildings (power station industry, transmitter). */
static constexpr SpriteID METRO_POWER_SPR_HALL = 0x7ff;        ///< Power station hall.
static constexpr SpriteID METRO_POWER_SPR_CHIMNEY = 0x802;     ///< Power station chimneys.
static constexpr SpriteID METRO_POWER_SPR_LOW = 0x805;         ///< Low power station building.
static constexpr SpriteID METRO_POWER_SPR_GROUND = 0xf54;      ///< Industry ground.

/** Toolbar icons (vanilla sprites for now; own sprites listed in DESIGN.md "Спрайты"). */
static constexpr SpriteID METRO_POWER_ICON_LINE = SPR_IMG_AUTOELRAIL;
static constexpr SpriteID METRO_POWER_ICON_LINE_REMOVE = SPR_IMG_REMOVE;
static const SpriteID _metro_power_building_icons[] = {
	SPR_IMG_DEPOT_ELRAIL, // Substation.
	SPR_IMG_INDUSTRY,     // Gas power plant.
	SPR_IMG_INDUSTRY,     // Nuclear power plant.
	SPR_IMG_PLANTTREES,   // Solar power plant.
	SPR_IMG_TRANSMITTER,  // Wind farm.
	SPR_IMG_TUNNEL_RAIL,  // Vent shaft.
};

#endif /* METRO_POWER_TABLES_H */
