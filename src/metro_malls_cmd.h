/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_malls_cmd.h Command definitions of the underground malls. */

#ifndef METRO_MALLS_CMD_H
#define METRO_MALLS_CMD_H

#include "command_type.h"
#include "station_type.h"

CommandCost CmdMetroBuildMall(DoCommandFlags flags, StationID station, uint8_t size);

DEF_CMD_TUPLE_NT(Commands::MetroBuildMall, CmdMetroBuildMall, {}, CommandType::LandscapeConstruction, CmdDataT<StationID, uint8_t>)

#endif /* METRO_MALLS_CMD_H */
