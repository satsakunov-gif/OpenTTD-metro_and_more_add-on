/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_power_cmd.h Command definitions of the metro mod power grid. */

#ifndef METRO_POWER_CMD_H
#define METRO_POWER_CMD_H

#include "command_type.h"

DEF_CMD_TUPLE   (Commands::MetroBuildPowerLine,   CmdMetroBuildPowerLine,   CMD_AUTO,                CommandType::LandscapeConstruction, CmdDataT<TileIndex, bool>)
DEF_CMD_TUPLE   (Commands::MetroBuildPowerObject, CmdMetroBuildPowerObject, CMD_NO_WATER | CMD_AUTO, CommandType::LandscapeConstruction, CmdDataT<uint8_t>)
DEF_CMD_TUPLE_NT(Commands::MetroSetPowerPriority, CmdMetroSetPowerPriority, {},                      CommandType::CompanySetting,        CmdDataT<bool>)

#endif /* METRO_POWER_CMD_H */
