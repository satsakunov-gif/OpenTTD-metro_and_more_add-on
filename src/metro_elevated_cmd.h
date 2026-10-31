/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_elevated_cmd.h Command definitions of the metro mod elevated monorail. */

#ifndef METRO_ELEVATED_CMD_H
#define METRO_ELEVATED_CMD_H

#include "command_type.h"
#include "rail_type.h"

DEF_CMD_TUPLE(Commands::MetroElevateRail,       CmdMetroElevateRail,       CMD_AUTO,                CommandType::LandscapeConstruction, CmdDataT<TileIndex, uint8_t>)
DEF_CMD_TUPLE(Commands::MetroElevateLine,       CmdMetroElevateLine,       CMD_AUTO,                CommandType::LandscapeConstruction, CmdDataT<TileIndex, uint8_t>)
DEF_CMD_TUPLE(Commands::MetroBuildElevatedSpan, CmdMetroBuildElevatedSpan, CMD_AUTO | CMD_NO_WATER, CommandType::LandscapeConstruction, CmdDataT<TileIndex, uint8_t, RailType>)

#endif /* METRO_ELEVATED_CMD_H */
