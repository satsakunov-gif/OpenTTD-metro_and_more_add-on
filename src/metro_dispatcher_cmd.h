/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_dispatcher_cmd.h Metro mod: command of the dispatcher (stage 9). */

#ifndef METRO_DISPATCHER_CMD_H
#define METRO_DISPATCHER_CMD_H

#include "command_type.h"
#include "vehicle_type.h"

CommandCost CmdMetroDispatchControl(DoCommandFlags flags, VehicleID vehicle, uint8_t action, int32_t value);

DEF_CMD_TUPLE_NT(Commands::MetroDispatchControl, CmdMetroDispatchControl, {}, CommandType::VehicleManagement, CmdDataT<VehicleID, uint8_t, int32_t>)

#endif /* METRO_DISPATCHER_CMD_H */
