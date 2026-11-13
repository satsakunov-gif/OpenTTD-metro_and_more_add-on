/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_civic.h Metro mod: government contracts and mayor elections (stage 9). */

#ifndef METRO_CIVIC_H
#define METRO_CIVIC_H

#include "economy_type.h"
#include "town_type.h"

#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct Town;

/* ---------------------------------------------------------------------------------------------- */
/* Government contracts                                                                           */
/* ---------------------------------------------------------------------------------------------- */

enum class MetroContractType : uint8_t {
	DistrictStation, ///< An underground station near the centre of a district.
	NeighbourLine,   ///< A line (one vehicle's orders) between the town and a neighbour town.
	Passengers,      ///< At least X passengers carried from the town every month, three months in a row.
	End,
};

enum class MetroContractState : uint8_t {
	Offered,  ///< Waiting for the answer of the player.
	Active,   ///< Accepted, running.
	Done,     ///< Fulfilled in time: paid.
	Failed,   ///< Deadline passed.
	Refused,  ///< Refused by the player.
	Expired,  ///< Not answered in time.
};

struct MetroContract {
	uint32_t id = 0;
	uint16_t town = 0;
	uint8_t type = 0;          ///< #MetroContractType.
	uint8_t state = 0;         ///< #MetroContractState.
	int32_t offer_date = 0;
	int32_t deadline = 0;      ///< Offered: last day to answer. Active: last day to fulfil.
	int32_t end_date = 0;      ///< Date of the result (done, failed, refused, expired).
	uint32_t target = 0;       ///< District station: centre tile. Line: the other town. Passengers: X per month.
	uint8_t progress = 0;      ///< Passengers: months in a row with X or more.
	uint32_t last_value = 0;   ///< Passengers carried last month (for the window).
	int64_t reward = 0;        ///< Money paid when fulfilled.
	int16_t rating = 0;        ///< Rating change when fulfilled (the failure costs the same).
	std::string title;         ///< Written by the AI mayor (or a template).
	std::string text;
	/* Not saved. */
	bool text_pending = false;
};

extern std::vector<MetroContract> _metro_contracts;
extern std::map<uint16_t, int32_t> _metro_contract_next_offer; ///< Town -> date of its next offer.

void MetroContractsReset();
void MetroContractsOnTownDeleted(TownID town);
std::string MetroContractSummary(const MetroContract &c);
std::string MetroContractStatus(const MetroContract &c);
void ShowMetroContractsWindow(TownID town = TownID::Invalid());

/* ---------------------------------------------------------------------------------------------- */
/* Mayor elections                                                                                */
/* ---------------------------------------------------------------------------------------------- */

struct MetroElectionCandidate {
	std::string name;
	uint8_t character = 0;   ///< Index of the mayor character.
	bool loyal = false;      ///< Loyal to the company.
	std::string program;
};

struct MetroElectionResult {
	int32_t date = 0;
	std::string winner;
	uint8_t character = 0;
	bool loyal = false;
	int16_t delta = 0;       ///< Rating change of the company.
	uint8_t chance = 0;      ///< Chance of the loyal candidate in percent.
};

struct MetroTownElections {
	uint16_t town = 0;
	int32_t next_date = 0;            ///< Date of the next elections.
	bool announced = false;           ///< The candidates are known (a month before).
	int32_t last_advertising = 0;     ///< Last advertising campaign of the company in the town.
	std::vector<MetroElectionCandidate> candidates;
	std::vector<MetroElectionResult> history;
	/* Not saved. */
	bool ai_pending = false;
};

extern std::map<uint16_t, MetroTownElections> _metro_elections;

void MetroElectionsReset();
void MetroElectionsOnTownDeleted(TownID town);
void MetroElectionsNoteAdvertising(const Town *t);
uint MetroElectionsLoyalChance(const Town *t, const MetroTownElections &e);
std::vector<std::string> MetroElectionsGetAuthorityLines(const Town *t);
void ShowMetroElectionsWindow(TownID town);

bool ConMetroCivic(std::span<std::string_view> argv);

#endif /* METRO_CIVIC_H */
