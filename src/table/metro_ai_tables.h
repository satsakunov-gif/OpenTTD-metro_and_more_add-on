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
#include "../metro_city.h"

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

/**
 * Loyalty level of the mayors (game setting). It changes the tone of the prompt and also limits the review in code,
 * so a harsh answer of the model cannot go past the level.
 */
struct MetroMayorLoyaltySpec {
	const char *prompt;      ///< Tone and strictness sent to the AI.
	int max_fall;            ///< Largest rating fall per review.
	int max_fall_damage;     ///< Largest rating fall per review when houses were demolished or trees cut since the last review.
	uint8_t min_needs;       ///< Fewest requests asked from the AI.
	uint8_t max_needs;       ///< Most requests kept from an answer.
};

/** Loyalty levels, indexed by #MetroMayorLoyalty. */
static const MetroMayorLoyaltySpec _metro_mayor_loyalty_specs[] = {
	{ "Loyalty: VERY LOYAL. You like the company and praise almost everything it does; your verdict is positive. "
		"Never give a rating below the current rating, unless the company demolished houses or cut trees since the last review. "
		"Requests are gentle wishes, not complaints.", 0, 200, 0, 1 },
	{ "Loyalty: LOYAL. You are friendly and grateful for any transport service; a new company that just started serving the town deserves a positive verdict. "
		"Criticise only serious problems (demolished houses, cut trees, removed roads, a town left without service), and mildly.", 50, 50, 1, 2 },
	{ "Loyalty: NORMAL. Be fair: praise good service, criticise real problems.", 200, 200, 0, 3 },
	{ "Loyalty: STRICT. You are demanding and critical: point out every weakness of the service, praise only excellent results, "
		"and expect the company to do more every quarter.", 200, 200, 1, 3 },
};
static_assert(std::size(_metro_mayor_loyalty_specs) == to_underlying(MetroMayorLoyalty::End));

/** Words that name a town service in a request of the AI (English and Russian, both cases); a request for a service the town is too small for is dropped. */
static const std::initializer_list<const char *> _metro_service_need_words[] = {
	{ "school", "School", "школ", "Школ" },
	{ "hospital", "Hospital", "больниц", "Больниц", "госпитал", "Госпитал", "поликлиник", "Поликлиник" },
	{ "universit", "Universit", "университет", "Университет", "вуз", "Вуз", "ВУЗ" },
};
static_assert(std::size(_metro_service_need_words) == METRO_SERVICE_COUNT);

/** Names of the services as sent to the AI, indexed by #MetroServiceType. */
static const char * const _metro_service_prompt_names[] = { "school", "hospital", "university" };
static_assert(std::size(_metro_service_prompt_names) == METRO_SERVICE_COUNT);

#endif /* METRO_AI_TABLES_H */
