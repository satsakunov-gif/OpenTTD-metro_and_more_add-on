/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_traffic_tables.h Data tables of the metro mod town traffic: private cars and pedestrians. */

#ifndef METRO_TRAFFIC_TABLES_H
#define METRO_TRAFFIC_TABLES_H

#include "../core/enum_type.hpp"
#include "../gfx_type.h"
#include "sprites.h"

/* Private cars: visual only, not saved; simulated in detail only around the screen. */
static constexpr uint32_t METRO_CARS_POPULATION_PER_CAR = 60;   ///< One car per this many residents of an active town.
static constexpr uint METRO_CARS_COVERAGE_CUT_PERCENT = 70;     ///< Good transport (all passengers carried) cuts the cars by this much.
static constexpr uint METRO_CARS_MAX_PER_TOWN = 120;            ///< Most cars of a town.
static constexpr uint METRO_CARS_MAX_TOTAL = 600;               ///< Most cars simulated at once (performance).
static constexpr uint METRO_CARS_SPAWN_PER_TICK = 2;            ///< Cars spawned per tick at most.
static constexpr uint METRO_CARS_MAX_TILES = 40;                ///< A car disappears after this many tiles without reaching its target.
static constexpr uint METRO_CARS_STEP_TICKS = 1;                ///< Ticks per pixel of movement (speed).
static constexpr int METRO_CARS_LANE_OFFSET = 3;                ///< Distance of the lane from the middle of the road, in pixels.
static constexpr uint METRO_CARS_TILE_CAPACITY = 2;             ///< Cars on one road tile and lane before the next has to wait.
static constexpr uint METRO_CARS_VIEW_MARGIN_TILES = 8;         ///< Cars are simulated in the viewports plus this margin.

/* Congestion: slows the player's road vehicles on town roads of active towns. */
static constexpr uint METRO_JAM_SLOWDOWN_PER_CAR = 15;          ///< Percent of speed lost per car on the tile (detailed simulation).
static constexpr uint METRO_JAM_MAX_SLOWDOWN = 60;             ///< Most percent of speed lost in a jam.
static constexpr uint METRO_JAM_CENTRE_PERCENT = 150;           ///< Estimate (whole town, also off screen): the town centre (inner half of the radius) is this much busier.
static constexpr uint METRO_JAM_OUTSKIRTS_PERCENT = 50;         ///< The same for the outskirts.
static constexpr uint METRO_JAM_ESTIMATE_TICKS = 256;           ///< The estimate of each town is refreshed this often.

/** Vanilla road vehicle sprites used for the private cars (bus and van bodies, by era). */
struct MetroCarSprite {
	int32_t from_year;    ///< First year of the sprite.
	SpriteID sprite;      ///< Sprite for #Direction::N; the other directions follow.
};
static const MetroCarSprite _metro_car_sprites[] = {
	{    0, 0xCD4 }, ///< Old bus.
	{    0, 0xD24 }, ///< Old van.
	{ 1965, 0xCDC }, ///< Bus.
	{ 1965, 0xD94 }, ///< Van.
	{ 1990, 0xCE4 }, ///< Modern bus.
	{ 1990, 0xDCC }, ///< Modern van.
};

/** Colours of the cars (recolour palettes of the companies). */
static constexpr uint METRO_CAR_COLOUR_COUNT = 16;

/* Pedestrians: visual only, not saved, drawn only at a close zoom. */
static constexpr uint32_t METRO_PEDESTRIANS_POPULATION_PER_PERSON = 80; ///< One pedestrian per this many residents.
static constexpr uint METRO_PEDESTRIANS_FLOW_PER_PERSON = 40;           ///< One more per this many passengers of the town's stations last month.
static constexpr uint METRO_PEDESTRIANS_MAX_PER_TOWN = 60;
static constexpr uint METRO_PEDESTRIANS_MAX_TOTAL = 400;
static constexpr uint METRO_PEDESTRIANS_STATION_RADIUS = 8;    ///< Pedestrians walk to stations within this many tiles.
static constexpr uint METRO_PEDESTRIANS_STEP_TICKS = 4;        ///< Ticks per pixel of walking.
static constexpr int METRO_PEDESTRIANS_SIDEWALK_OFFSET = 6;    ///< Distance of the sidewalk from the middle of the road, in pixels.
static constexpr uint METRO_PEDESTRIANS_MAX_STEPS = 400;       ///< A pedestrian disappears after this many pixels.
static constexpr uint METRO_PEDESTRIANS_SPAWN_PER_STEP = 1;    ///< Pedestrians spawned per town and walking step at most.

/* Stadium on match day (only the day of the match): more cars, jams and a crowd around the stadium. */
static constexpr uint METRO_MATCH_AREA_TILES = 5;               ///< Jams and the crowd within this many tiles of the stadium.
static constexpr uint METRO_MATCH_TO_STADIUM_PERCENT = 50;      ///< Share of the town's cars and pedestrians driving to or from the stadium.
static constexpr uint METRO_MATCH_SPAWN_PER_STEP = 4;           ///< Pedestrians spawned per walking step while a crowd gathers.

/** Effect of a match on the town traffic, indexed by #MetroMatchLevel. */
struct MetroMatchTrafficSpec {
	uint16_t car_percent;   ///< Cars of the town (and their limit) in percent of a usual day.
	uint8_t jam_percent;    ///< Extra speed lost by the player's road vehicles near the stadium.
	uint8_t crowd;          ///< Extra pedestrians (fans) of the town.
};
static const MetroMatchTrafficSpec _metro_match_traffic[] = {
	{ 150, 20, 20 }, ///< Local.
	{ 200, 30, 40 }, ///< Regional.
	{ 300, 40, 60 }, ///< Final.
};

#endif /* METRO_TRAFFIC_TABLES_H */
