/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_ai_tables.h Data tables of the metro mod AI town rating: mayor characters and change weights. */

#ifndef METRO_AI_TABLES_H
#define METRO_AI_TABLES_H

#include "../metro_ai.h"

/** Character of a town mayor; sent to the AI and shown in the local authority window. */
struct MetroMayorCharacter {
	StringID name;      ///< Name shown to the player.
	const char *prompt; ///< Description sent to the AI (English, the AI answers in the game language).
};

/** Mayor characters. A town gets a random one on its first review; elections (stage 9) will change it. */
static const MetroMayorCharacter _metro_mayor_characters[] = {
	{ STR_METRO_AI_MAYOR_STRICT,     "strict and demanding, rarely satisfied, harsh about any damage to the town" },
	{ STR_METRO_AI_MAYOR_KIND,       "kind and optimistic, praises effort, forgives small damage" },
	{ STR_METRO_AI_MAYOR_BUREAUCRAT, "bureaucrat, cares about order, paperwork and statistics, dry tone" },
	{ STR_METRO_AI_MAYOR_ECOLOGIST,  "ecologist, cares about trees, rivers and noise, likes public transport" },
	{ STR_METRO_AI_MAYOR_PRAGMATIST, "pragmatist, judges only by numbers: passengers carried and station service" },
	{ STR_METRO_AI_MAYOR_POPULIST,   "populist, speaks emotionally for ordinary citizens, loves visible new projects" },
};

/** Weight of each player change in a town; the sum decides whether the town is worth a new AI request. */
static const uint8_t _metro_town_change_weights[] = {
	3, ///< MetroTownChange::HouseDemolished
	1, ///< MetroTownChange::TreeRemoved
	1, ///< MetroTownChange::TreePlanted
	2, ///< MetroTownChange::RoadRemoved
	5, ///< MetroTownChange::TunnelBridgeRemoved
	2, ///< MetroTownChange::TunnelBridgeImproved
	3, ///< MetroTownChange::RiverRemoved
	2, ///< MetroTownChange::TownAction
	3, ///< MetroTownChange::StationChanged
};
static_assert(std::size(_metro_town_change_weights) == to_underlying(MetroTownChange::End));

/** Short names of the changes as sent to the AI. */
static const char * const _metro_town_change_names[] = {
	"houses_demolished",
	"trees_cut",
	"trees_planted",
	"town_roads_removed",
	"town_bridges_or_tunnels_removed",
	"town_bridges_rebuilt",
	"rivers_or_canals_removed",
	"local_authority_actions",
	"stations_built_or_removed",
};
static_assert(std::size(_metro_town_change_names) == to_underlying(MetroTownChange::End));

#endif /* METRO_AI_TABLES_H */
