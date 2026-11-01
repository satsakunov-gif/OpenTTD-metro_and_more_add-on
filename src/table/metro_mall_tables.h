/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_mall_tables.h Data tables of the underground malls (stage 9, decisions of the user). */

#ifndef METRO_MALL_TABLES_H
#define METRO_MALL_TABLES_H

#include "strings.h"

/** A size of an underground mall. */
struct MetroMallSpec {
	StringID name;
	uint8_t price_factor;          ///< Price: this many times the megahub upgrade of the station.
	uint8_t income_per_passenger;  ///< Money per passenger of the station per month (at prices of the game start).
};

/** Sizes S, M, L (index + 1 is the saved size). */
static const MetroMallSpec _metro_mall_specs[] = {
	{ STR_METRO_MALL_SIZE_S, 1, 2 },
	{ STR_METRO_MALL_SIZE_M, 2, 4 },
	{ STR_METRO_MALL_SIZE_L, 4, 8 },
};

static constexpr uint METRO_MALL_UPKEEP_PERCENT = 1;          ///< Upkeep per month, percent of the price.
static constexpr uint METRO_MALL_INTERCHANGE_BONUS_PERCENT = 50; ///< Extra income at an interchange.
static constexpr uint METRO_MALL_REFUND_PERCENT = 25;         ///< Refund when the mall is closed.

#endif /* METRO_MALL_TABLES_H */
