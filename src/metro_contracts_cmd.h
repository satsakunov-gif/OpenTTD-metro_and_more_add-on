/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_contracts_cmd.h Metro mod: command of the government contracts (stage 9). */

#ifndef METRO_CONTRACTS_CMD_H
#define METRO_CONTRACTS_CMD_H

#include "command_type.h"

CommandCost CmdMetroContractAnswer(DoCommandFlags flags, uint32_t contract, bool accept);

DEF_CMD_TUPLE_NT(Commands::MetroContractAnswer, CmdMetroContractAnswer, {}, CommandType::OtherManagement, CmdDataT<uint32_t, bool>)

#endif /* METRO_CONTRACTS_CMD_H */
