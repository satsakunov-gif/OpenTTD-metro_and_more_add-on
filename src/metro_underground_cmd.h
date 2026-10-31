/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_underground_cmd.h Command definitions of the metro mod underground. */

#ifndef METRO_UNDERGROUND_CMD_H
#define METRO_UNDERGROUND_CMD_H

#include "command_type.h"
#include "track_type.h"

DEF_CMD_TUPLE(Commands::MetroBuildUndergroundTrack, CmdMetroBuildUndergroundTrack, CMD_AUTO,                CommandType::LandscapeConstruction, CmdDataT<TileIndex, uint8_t, Track, bool>)
DEF_CMD_TUPLE(Commands::MetroBuildIncline,          CmdMetroBuildIncline,          CMD_AUTO,                CommandType::LandscapeConstruction, CmdDataT<TileIndex, uint8_t>)
DEF_CMD_TUPLE(Commands::MetroBuildEntrance,         CmdMetroBuildEntrance,         CMD_NO_WATER | CMD_AUTO, CommandType::LandscapeConstruction, CmdDataT<uint8_t>)

#endif /* METRO_UNDERGROUND_CMD_H */
