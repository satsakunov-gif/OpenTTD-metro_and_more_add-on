/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_cinema_tables.h Metro mod: numbers of the cinema camera (stage 9, decisions of the user). */

#ifndef METRO_CINEMA_TABLES_H
#define METRO_CINEMA_TABLES_H

/* All distances are in world pixels: a tile is 16 of them. */
static constexpr uint METRO_CINEMA_SWITCH_MIN_MS = 10000;  ///< Random mode: a new target after 10..20 seconds.
static constexpr uint METRO_CINEMA_SWITCH_MAX_MS = 20000;
static constexpr int METRO_CINEMA_BEHIND_PX = 24;          ///< "Behind": the camera is 1.5 tiles behind the target along its motion.
static constexpr int METRO_CINEMA_ORBIT_MIN_PX = 64;       ///< "Orbit": circle of 4..6 tiles around the target.
static constexpr int METRO_CINEMA_ORBIT_MAX_PX = 96;
static constexpr uint METRO_CINEMA_ORBIT_PERIOD_MS = 24000; ///< One full circle of the orbit.
static constexpr uint METRO_CINEMA_ORBIT_BREATH_MS = 9000;  ///< The orbit radius swings between min and max in this time.
static constexpr double METRO_CINEMA_SMOOTH_MS = 450.0;     ///< Time constant of the camera catching up with its aim.
static constexpr int METRO_CINEMA_JUMP_PX = 64 * 16;        ///< A new target farther than 64 tiles: cut instead of gliding.
static constexpr uint METRO_CINEMA_HINT_MS = 5000;          ///< The key hint stays this long on the full screen view.
static constexpr uint METRO_CINEMA_FAST_TRAINS = 5;         ///< Random mode picks among this many fastest trains.

/** Direction (#Direction order N, NE, E, SE, S, SW, W, NW) as a step in world x and y. */
static constexpr int8_t _metro_cinema_dir_vec[8][2] = {
	{-1, -1}, {-1, 0}, {-1, 1}, {0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1},
};

#endif /* METRO_CINEMA_TABLES_H */
