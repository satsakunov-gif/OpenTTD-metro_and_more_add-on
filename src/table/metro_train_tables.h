/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_train_tables.h Data tables of the metro trains (stage 8, step 6). */

#ifndef METRO_TRAIN_TABLES_H
#define METRO_TRAIN_TABLES_H

#include "../newgrf_type.h"
#include "strings.h"

/** Layers a train runs on (bit 0 = layer -1, bit 1 = -2, bit 2 = -3). */
static constexpr uint8_t ML1 = 1, ML2 = 2, ML3 = 4, MLALL = ML1 | ML2 | ML3;

/**
 * A metro series: a head (with passengers, cab at both ends unless @c single) and a car of the same series refittable
 * to any cargo. Prices about those of the electric trains of the same years. Sprites are vanilla multiple units for now.
 */
struct MetroTrainSpec {
	int32_t intro_year;    ///< Year of introduction; 0 = when the layer @c intro_layer opens.
	uint8_t intro_layer;   ///< Layer whose opening introduces the series (with @c intro_year 0).
	StringID name;         ///< Head.
	StringID car_name;     ///< Car.
	uint8_t layers;        ///< Layers the series runs on (ML*).
	uint16_t speed_kmh;    ///< Top speed.
	uint8_t capacity;      ///< Passengers of the head and of a car.
	uint8_t image_index;   ///< Vanilla rail vehicle sprite set (head and car).
	bool single;           ///< One long car without a rear cab (no articulation without NewGRF).
	uint8_t cost_factor;   ///< Head price factor (both cabs).
	uint8_t running_cost;  ///< Head running cost factor.
	uint16_t power_hp;     ///< Head power (both cabs).
	uint8_t weight_t;      ///< Head weight per cab.
	uint8_t car_cost;      ///< Car price factor.
	uint8_t car_weight_t;  ///< Car weight.
};

static const MetroTrainSpec _metro_trains[] = {
	{ 1960, 0, STR_METRO_TRAIN_A,        STR_METRO_TRAIN_A_CAR,        ML1,       60,  90,  8, false, 20, 120,  800, 35, 180, 25 },
	{    0, 2, STR_METRO_TRAIN_E,        STR_METRO_TRAIN_E_CAR,        ML1 | ML2, 80, 110, 10, false, 24, 140, 1200, 36, 190, 26 },
	{ 1985, 0, STR_METRO_TRAIN_PREMETRO, STR_METRO_TRAIN_PREMETRO_CAR, ML1 | ML2, 70,  80, 16, false, 16, 100,  900, 30, 150, 22 },
	{ 2000, 0, STR_METRO_TRAIN_2000,     STR_METRO_TRAIN_2000_CAR,     MLALL,    100, 140, 21, true,  34, 180, 2000, 45, 220, 30 },
	{ 2020, 0, STR_METRO_TRAIN_DIAMETR,  STR_METRO_TRAIN_DIAMETR_CAR,  MLALL,    140, 120, 23, true,  40, 200, 3000, 45, 230, 30 },
	{    0, 3, STR_METRO_TRAIN_DEEP,     STR_METRO_TRAIN_DEEP_CAR,     ML3,       90, 120,  6, false, 36, 190, 1800, 42, 220, 28 },
	{ 2040, 0, STR_METRO_TRAIN_VACUUM,   STR_METRO_TRAIN_VACUUM_CAR,   ML3,      200, 100, 26, false, 70, 250, 6000, 30, 255, 20 },
	{ 2060, 0, STR_METRO_TRAIN_CAPSULE,  STR_METRO_TRAIN_CAPSULE_CAR,  MLALL,    160, 160, 26, false, 50, 220, 3500, 40, 240, 26 },
};

static const GrfID METRO_TRAIN_GRFID{"MTRT"}; ///< Scope of the metro trains in the engine ID mapping (like a NewGRF).
static constexpr uint8_t METRO_TRAIN_VEHICLE_LIFE = 30; ///< Years a train lasts.
static constexpr uint8_t METRO_TRAIN_MODEL_LIFE = 30;   ///< Years a model is sold.
static constexpr uint8_t METRO_TRAIN_DECAY_SPEED = 20;  ///< Reliability decay.
static constexpr uint8_t METRO_TRAIN_LOAD_AMOUNT = 10;  ///< Passengers loaded per step (wide doors).

#endif /* METRO_TRAIN_TABLES_H */
