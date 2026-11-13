/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_civic_tables.h Metro mod: numbers of the government contracts and the mayor elections (stage 9, decisions of the user). */

#ifndef METRO_CIVIC_TABLES_H
#define METRO_CIVIC_TABLES_H

#include "../metro_civic.h"

/* Government contracts. */
static constexpr uint32_t METRO_CONTRACT_MIN_POPULATION = 5000; ///< Active towns from this size get offers.
static constexpr uint METRO_CONTRACT_OFFER_MIN_DAYS = 365;      ///< An offer every 1..2 years per town.
static constexpr uint METRO_CONTRACT_OFFER_MAX_DAYS = 730;
static constexpr uint METRO_CONTRACT_FAIL_MIN_DAYS = 730;       ///< After a failure the next offer comes in 2..4 years.
static constexpr uint METRO_CONTRACT_FAIL_MAX_DAYS = 1460;
static constexpr uint METRO_CONTRACT_ANSWER_DAYS = 30;          ///< Days to accept or refuse; refusing costs nothing.
static constexpr int16_t METRO_CONTRACT_RATING = 150;           ///< Rating for a fulfilled contract; a failure costs as much.
static constexpr uint METRO_CONTRACT_DISTRICT_RADIUS = 8;       ///< The station must be this near the centre of the district.
static constexpr uint METRO_CONTRACT_NEIGHBOUR_DISTANCE = 100;  ///< The neighbour town of a line is at most this far.
static constexpr uint METRO_CONTRACT_PASSENGER_PERCENT = 10;    ///< X passengers a month = this percent of the population.
static constexpr uint8_t METRO_CONTRACT_PASSENGER_MONTHS = 3;   ///< Months in a row with X or more.
/* Reward unit: the yearly upkeep of a megahub, taken as 12 x 1% (the mall upkeep rate) of the megahub upgrade of a reference station. */
static constexpr uint METRO_CONTRACT_REF_PLATFORMS = 8;         ///< Platform tiles of the reference station (on -1).
static constexpr uint METRO_CONTRACT_YEAR_UPKEEP_PERCENT = 12;

/** One kind of contract. */
struct MetroContractSpec {
	uint16_t deadline_days; ///< Days to fulfil after accepting.
	uint8_t reward_years;   ///< Reward in yearly upkeeps of a megahub.
	const char *prompt;     ///< What it is, for the AI mayor.
};

static const MetroContractSpec _metro_contract_specs[] = {
	{ 365, 2, "build an underground (metro) station of the company near the centre of a district of the town" },
	{ 730, 4, "open a transport line of the company between this town and a neighbour town" },
	{ 365, 3, "carry at least X passengers from the town every month, three months in a row" },
};
static_assert(std::size(_metro_contract_specs) == to_underlying(MetroContractType::End));

/* Mayor elections. */
static constexpr uint32_t METRO_ELECTION_MIN_POPULATION = 2000; ///< Active towns from this size elect a mayor.
static constexpr int32_t METRO_ELECTION_PERIOD_YEARS = 4;
static constexpr uint METRO_ELECTION_ANNOUNCE_DAYS = 30;        ///< The news and the candidates come a month before.
static constexpr uint METRO_ELECTION_FIRST_MIN_DAYS = 60;       ///< Old towns (activated long ago) get their first elections in 60..365 days.
static constexpr uint METRO_ELECTION_FIRST_MAX_DAYS = 365;
static constexpr uint METRO_ELECTION_AD_DAYS = 30;              ///< An advertising campaign this many days before the vote helps.
static constexpr int METRO_ELECTION_LOYAL_BASE = 20;            ///< Chance of the loyal candidate: 20% + (rating + 1000) / 2000 x 60% ...
static constexpr int METRO_ELECTION_LOYAL_RATING_SPAN = 60;
static constexpr int METRO_ELECTION_AD_BONUS = 10;              ///< ... + 10% after advertising ...
static constexpr int METRO_ELECTION_LOYAL_MIN = 5;              ///< ... between 5% and 90%.
static constexpr int METRO_ELECTION_LOYAL_MAX = 90;
static constexpr int16_t METRO_ELECTION_LOYAL_WIN = 100;        ///< Rating when the loyal candidate wins.
static constexpr int16_t METRO_ELECTION_LOYAL_LOSE = -100;      ///< Rating when another one wins.

/** The setting "Mayor loyalty" changes the elections, indexed by #MetroMayorLoyalty. */
struct MetroElectionLoyaltySpec {
	int chance;     ///< Added to the chance of the loyal candidate.
	int16_t lose;   ///< Rating when another candidate wins.
};

static const MetroElectionLoyaltySpec _metro_election_loyalty[] = {
	{ 15, 0 },     ///< Very loyal: more chance, no penalty.
	{ 0, -100 },   ///< Loyal.
	{ 0, -100 },   ///< Normal.
	{ -10, -150 }, ///< Strict: less chance, a bigger penalty.
};

/** Mayor characters a loyal candidate may have (indices of the mayor characters: kind, pragmatist, populist). */
static const uint8_t _metro_election_loyal_characters[] = { 1, 4, 5 };

/** Programs of the candidates without the AI, by mayor character (strict, kind, bureaucrat, ecologist, pragmatist, populist). */
static const StringID _metro_election_programs[] = {
	STR_METRO_ELECTION_PROGRAM_STRICT, STR_METRO_ELECTION_PROGRAM_KIND, STR_METRO_ELECTION_PROGRAM_BUREAUCRAT,
	STR_METRO_ELECTION_PROGRAM_ECOLOGIST, STR_METRO_ELECTION_PROGRAM_PRAGMATIST, STR_METRO_ELECTION_PROGRAM_POPULIST,
};

/** Names of the candidates without the AI: Russian for a Russian game, English otherwise. */
struct MetroCandidateFirstName {
	const char *name;
	bool female;
};

static const MetroCandidateFirstName _metro_names_first_ru[] = {
	{"Анна", true}, {"Мария", true}, {"Елена", true}, {"Ольга", true}, {"Татьяна", true}, {"Ирина", true},
	{"Алексей", false}, {"Дмитрий", false}, {"Сергей", false}, {"Игорь", false}, {"Павел", false}, {"Николай", false},
};
/** Surnames: male and female forms. */
static const std::pair<const char *, const char *> _metro_names_last_ru[] = {
	{"Иванов", "Иванова"}, {"Смирнов", "Смирнова"}, {"Кузнецов", "Кузнецова"}, {"Попов", "Попова"},
	{"Соколов", "Соколова"}, {"Лебедев", "Лебедева"}, {"Морозов", "Морозова"}, {"Волков", "Волкова"},
	{"Зайцев", "Зайцева"}, {"Орлов", "Орлова"}, {"Белов", "Белова"}, {"Громов", "Громова"},
};
static const MetroCandidateFirstName _metro_names_first_en[] = {
	{"Anna", true}, {"Mary", true}, {"Helen", true}, {"Olivia", true}, {"Grace", true}, {"Emma", true},
	{"Thomas", false}, {"James", false}, {"Peter", false}, {"George", false}, {"Henry", false}, {"Samuel", false},
};
static const std::pair<const char *, const char *> _metro_names_last_en[] = {
	{"Smith", "Smith"}, {"Turner", "Turner"}, {"Walker", "Walker"}, {"Hughes", "Hughes"},
	{"Baker", "Baker"}, {"Wright", "Wright"}, {"Cooper", "Cooper"}, {"Fletcher", "Fletcher"},
	{"Harper", "Harper"}, {"Mills", "Mills"}, {"Porter", "Porter"}, {"Ward", "Ward"},
};

#endif /* METRO_CIVIC_TABLES_H */
