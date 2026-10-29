/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_ai.h Metro mod: local authority rating and yearly report written by an LLM (Gemma / DeepSeek). */

#ifndef METRO_AI_H
#define METRO_AI_H

#include "command_type.h"
#include "date_type.h"
#include "strings_type.h"
#include "town_type.h"

#include <array>
#include <map>
#include <vector>

struct Town;

/** Player changes in a town that are reported to the AI. Weights and names: table/metro_ai_tables.h. */
enum class MetroTownChange : uint8_t {
	HouseDemolished,
	TreeRemoved,
	TreePlanted,
	RoadRemoved,
	TunnelBridgeRemoved,
	TunnelBridgeImproved,
	RiverRemoved,
	TownAction,
	StationChanged,
	End,
};

static constexpr uint METRO_TOWN_CHANGE_COUNT = static_cast<uint>(MetroTownChange::End);

/** Why the last AI request failed. */
enum class MetroAiFailure : uint8_t {
	None,      ///< No failure.
	Http,      ///< The server answered with an HTTP error status.
	Network,   ///< No connection to the server.
	Timeout,   ///< The server did not answer in time.
	BadAnswer, ///< The answer is not the expected JSON.
};

/** AI state of one town (saved in the MTAI chunk). */
struct MetroTownAiState {
	uint16_t town = 0;                 ///< Town index.
	uint8_t mayor = 0;                 ///< Index in _metro_mayor_characters.
	bool has_review = false;           ///< Whether the AI has reviewed the town at least once.
	int16_t ai_rating = 0;             ///< Rating the AI asked for in the last review.
	int16_t last_delta = 0;            ///< Change applied to the company rating by the last review.
	int32_t review_date = 0;           ///< Calendar date of the last review.
	std::string verdict;               ///< Short verdict of the last review.
	std::string explanation;           ///< Paragraph from the mayor.
	std::string needs;                 ///< Needs of the town, separated by '\n'.
	std::array<uint16_t, METRO_TOWN_CHANGE_COUNT> changes{}; ///< Player changes since the last review.
	uint32_t last_population = 0;      ///< Population at the last review.
	uint16_t last_station_count = 0;   ///< Company stations near the town at the last review.

	/* Not saved. */
	bool pending = false;              ///< A request for this town is queued or running.
	MetroAiFailure failure = MetroAiFailure::None; ///< Why the last request failed; the old review is kept.
	int failure_status = 0;            ///< HTTP status of an #MetroAiFailure::Http failure.
	std::string failure_text;          ///< Error message of a #MetroAiFailure::Network failure.

	uint ChangeWeight() const;
};

/** Yearly "results of the year" article (saved in the MTYR chunk). */
struct MetroYearReport {
	int32_t year = 0;   ///< Calendar year of the report.
	std::string title;  ///< Headline.
	std::string text;   ///< Article.
};

extern std::map<uint16_t, MetroTownAiState> _metro_town_ai;
extern std::vector<MetroYearReport> _metro_year_reports;

void MetroAiReset();
bool MetroAiIsEnabled();

void MetroAiNoteTownChange(const Town *t, MetroTownChange change, DoCommandFlags flags);
void MetroAiOnTownDeleted(TownID town);
void MetroAiOnAuthorityWindowOpened(const Town *t);
std::vector<std::string> MetroAiGetAuthorityLines(const Town *t);

const MetroYearReport *MetroAiGetYearReport(int32_t year);
int32_t MetroAiGetPendingYearReport();
int32_t MetroAiGetFailedYearReport(std::string &reason);
void ShowMetroYearReportWindow(int32_t year = -1);

bool ConMetroAi(std::span<std::string_view> argv);

#endif /* METRO_AI_H */
