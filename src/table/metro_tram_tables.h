/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_tram_tables.h Data tables of the metro mod built-in trams. */

#ifndef METRO_TRAM_TABLES_H
#define METRO_TRAM_TABLES_H

#include "../sound_type.h"
#include "../newgrf_type.h"
#include "strings.h"

/**
 * A built-in tram. Prices: about 70% of a bus of the same years per passenger, but no more than the field allows (255).
 * Sprites are vanilla bus sprites for now.
 */
struct MetroTramSpec {
	int32_t intro_year;    ///< Year of introduction (randomised a little like vanilla engines).
	StringID name;
	uint8_t image_index;   ///< Vanilla road vehicle sprite set.
	uint8_t cost_factor;   ///< Purchase price factor.
	uint8_t running_cost;  ///< Running cost factor.
	SoundID sound;         ///< Departure sound.
	uint16_t speed_kmh;    ///< Top speed, km/h.
	uint8_t capacity;      ///< Passengers.
	uint8_t weight_t;      ///< Weight, tonnes.
	uint8_t power_10hp;    ///< Power in 10 hp.
};

static const MetroTramSpec _metro_trams[] = {
	{ 1950, STR_METRO_TRAM_T50,       0, 163, 123, SND_1C_DEPARTURE_OLD_BUS,     60,  60, 16, 15 },
	{ 1970, STR_METRO_TRAM_T70,      17, 255, 255, SND_1C_DEPARTURE_OLD_BUS,     70, 110, 25, 25 },
	{ 2000, STR_METRO_TRAM_LOWFLOOR, 34, 255, 255, SND_1B_DEPARTURE_MODERN_BUS,  80, 160, 35, 40 },
	{ 2040, STR_METRO_TRAM_AERO,     34, 255, 255, SND_1B_DEPARTURE_MODERN_BUS, 100, 200, 40, 60 },
};

static const GrfID METRO_TRAM_GRFID{"MTR1"}; ///< Scope of the built-in trams in the engine ID mapping (like a NewGRF).
static constexpr uint8_t METRO_TRAM_VEHICLE_LIFE = 20;       ///< Years a tram lasts.
static constexpr uint8_t METRO_TRAM_MODEL_LIFE = 30;         ///< Years a model is sold.
static constexpr uint8_t METRO_TRAM_DECAY_SPEED = 20;        ///< Reliability decay, as vanilla buses.

#endif /* METRO_TRAM_TABLES_H */
