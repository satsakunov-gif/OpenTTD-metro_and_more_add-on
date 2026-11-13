/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_dispatcher.h Metro mod: dispatcher, manual control of one vehicle (stage 9). */

#ifndef METRO_DISPATCHER_H
#define METRO_DISPATCHER_H

#include "direction_type.h"
#include "track_type.h"
#include "vehicle_type.h"

#include <span>
#include <string_view>

struct Train;
struct RoadVehicle;
struct Ship;
struct Aircraft;

/** Choice at the next junction. */
enum class MetroDispatchTurn : uint8_t {
	Auto,     ///< The pathfinder decides.
	Left,
	Straight,
	Right,
};

/** What the dispatcher command does. */
enum class MetroDispatchAction : uint8_t {
	Start,    ///< Take the vehicle under control.
	Stop,     ///< Give it back.
	Speed,    ///< Target speed in km-ish/h (value), -1 = no limit.
	Turn,     ///< #MetroDispatchTurn (value).
	Course,   ///< Ship course: #Direction (value), -1 = the pathfinder.
	PassRed,  ///< Train: pass the red signal ahead, for a fine.
	Takeoff,  ///< Aircraft: leave the airport now.
	Land,     ///< Aircraft: land at the nearest hangar.
};

void MetroDispatchReset();
bool MetroDispatchIsControlled(const Vehicle *v);
int MetroDispatchAdjustSpeed(const Vehicle *front, int max_speed);
TrackBits MetroDispatchFilterTrainTracks(const Train *t, DiagDirection enterdir, TrackBits tracks);
Trackdir MetroDispatchChooseRoadTrackdir(const RoadVehicle *v, DiagDirection enterdir, TrackdirBits trackdirs);
Track MetroDispatchChooseShipTrack(const Ship *v, TrackBits tracks);
void MetroDispatchStart(VehicleID vehicle);
bool ConMetroDispatch(std::span<std::string_view> argv);

#endif /* METRO_DISPATCHER_H */
