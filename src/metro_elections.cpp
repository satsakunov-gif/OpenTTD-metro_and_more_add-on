/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_elections.cpp Metro mod: mayor elections (stage 9, decisions of the user).
 *
 * Active towns of 2000 people and more elect a mayor every 4 years (counted from the activation). A month before the
 * news names 2-3 candidates: the game picks their characters (one of them loyal to the company), the AI writes names
 * and programs (a table without the AI). The loyal candidate wins with 20% + (rating + 1000) / 2000 x 60%, +10% after
 * an advertising campaign in the last month, between 5% and 90%; the others share the rest. A loyal winner gives
 * +100 rating, another one -100; the setting "Mayor loyalty" changes this (very loyal: +15% chance and no penalty,
 * strict: -10% chance and -150). The new mayor changes the character of the reviews and his program becomes the needs
 * of the town. Numbers: table/metro_civic_tables.h.
 */

#include "stdafx.h"
#include "metro_civic.h"
#include "metro_ai.h"
#include "metro_city.h"
#include "company_base.h"
#include "company_func.h"
#include "console_func.h"
#include "console_type.h"
#include "core/random_func.hpp"
#include "core/string_consumer.hpp"
#include "gfx_func.h"
#include "language.h"
#include "news_func.h"
#include "settings_type.h"
#include "strings_func.h"
#include "timer/timer.h"
#include "timer/timer_game_calendar.h"
#include "town.h"
#include "window_func.h"
#include "window_gui.h"
#include "zoom_func.h"
#include "table/strings.h"
#include "table/metro_civic_tables.h"
#include "3rdparty/nlohmann/json.hpp"

#include "safeguards.h"

std::map<uint16_t, MetroTownElections> _metro_elections;

static_assert(std::size(_metro_election_loyalty) == to_underlying(MetroMayorLoyalty::End));

void MetroElectionsReset()
{
	_metro_elections.clear();
}

void MetroElectionsOnTownDeleted(TownID town)
{
	_metro_elections.erase(town.base());
}

static int32_t Today()
{
	return CalTime::CurDate().base();
}

/** The same day @p years later. */
static int32_t YearsLater(int32_t date, int32_t years)
{
	CalTime::YearMonthDay ymd = CalTime::ConvertDateToYMD(CalTime::Date{date});
	return CalTime::ConvertYMDToDate(ymd.year + years, ymd.month, std::min<uint8_t>(ymd.day, 28)).base();
}

static const MetroElectionLoyaltySpec &LoyaltySpec()
{
	return _metro_election_loyalty[std::min<uint>(_settings_game.economy.metro_mayor_loyalty, std::size(_metro_election_loyalty) - 1)];
}

void MetroElectionsNoteAdvertising(const Town *t)
{
	if (_current_company != _local_company) return;
	auto it = _metro_elections.find(t->index.base());
	if (it != _metro_elections.end()) it->second.last_advertising = Today();
}

/** Chance of the loyal candidate in percent now. */
uint MetroElectionsLoyalChance(const Town *t, const MetroTownElections &e)
{
	int rating = t->have_ratings.Test(_local_company) ? t->ratings[_local_company] : 0;
	int chance = METRO_ELECTION_LOYAL_BASE + (rating + 1000) * METRO_ELECTION_LOYAL_RATING_SPAN / 2000;
	if (e.last_advertising != 0 && e.next_date - e.last_advertising <= static_cast<int32_t>(METRO_ELECTION_AD_DAYS)) chance += METRO_ELECTION_AD_BONUS;
	chance += LoyaltySpec().chance;
	return static_cast<uint>(Clamp(chance, METRO_ELECTION_LOYAL_MIN, METRO_ELECTION_LOYAL_MAX));
}

/* ---------------------------------------------------------------------------------------------- */
/* Candidates                                                                                     */
/* ---------------------------------------------------------------------------------------------- */

static bool IsRussian()
{
	return _current_language != nullptr && std::string_view(_current_language->isocode).starts_with("ru");
}

/** A name from the tables (the game language decides Russian or English). */
static std::string RandomName()
{
	if (IsRussian()) {
		const MetroCandidateFirstName &first = _metro_names_first_ru[InteractiveRandomRange(static_cast<uint32_t>(std::size(_metro_names_first_ru)))];
		const auto &last = _metro_names_last_ru[InteractiveRandomRange(static_cast<uint32_t>(std::size(_metro_names_last_ru)))];
		return fmt::format("{} {}", first.name, first.female ? last.second : last.first);
	}
	const MetroCandidateFirstName &first = _metro_names_first_en[InteractiveRandomRange(static_cast<uint32_t>(std::size(_metro_names_first_en)))];
	const auto &last = _metro_names_last_en[InteractiveRandomRange(static_cast<uint32_t>(std::size(_metro_names_last_en)))];
	return fmt::format("{} {}", first.name, first.female ? last.second : last.first);
}

/** Ask the AI for the names and programs of the candidates; the table ones stay if it fails. */
static void RequestCandidateTexts(const Town *t, MetroTownElections &e)
{
	if (!MetroAiIsEnabled()) return;
	e.ai_pending = true;
	std::string system = "You write the candidates of a mayor election of a town in the transport tycoon game OpenTTD. "
		"Answer with one JSON object only, no markdown: {\"candidates\": [{\"name\": first name and surname, \"program\": the election promise, at most 12 words}, ...]} "
		"with exactly the candidates given, in the same order, each matching the personality given. "
		"A candidate loyal to the transport company promises to work with it; the others promise their own priorities. "
		"Write names and programs in " + MetroAiLanguageName() + ", names typical for that language.";
	std::string user = fmt::format("town: {}; population: {}\n", GetString(STR_TOWN_NAME, t->index), t->cache.population);
	for (size_t i = 0; i < e.candidates.size(); i++) {
		user += fmt::format("candidate {}: personality: {}; loyal to the transport company: {}\n", i + 1, MetroAiMayorPrompt(e.candidates[i].character), e.candidates[i].loyal ? "yes" : "no");
	}
	uint16_t town = t->index.base();
	size_t count = e.candidates.size();
	MetroAiRequestJson(std::move(system), std::move(user), 800, [town, count](const std::string *json) {
		auto it = _metro_elections.find(town);
		if (it == _metro_elections.end()) return;
		MetroTownElections &e = it->second;
		e.ai_pending = false;
		if (json == nullptr || !e.announced || e.candidates.size() != count) return;
		nlohmann::json j = nlohmann::json::parse(*json, nullptr, false);
		if (!j.is_object() || !j.contains("candidates") || !j["candidates"].is_array()) return;
		const nlohmann::json &list = j["candidates"];
		for (size_t i = 0; i < count && i < list.size(); i++) {
			if (!list[i].is_object()) continue;
			if (list[i].contains("name") && list[i]["name"].is_string()) {
				std::string name = list[i]["name"].get<std::string>();
				if (!name.empty() && name.size() < 80) e.candidates[i].name = std::move(name);
			}
			if (list[i].contains("program") && list[i]["program"].is_string()) {
				std::string program = list[i]["program"].get<std::string>();
				if (!program.empty() && program.size() < 300) e.candidates[i].program = std::move(program);
			}
		}
		InvalidateWindowData(WindowClass::MetroElections, town);
		InvalidateWindowData(WindowClass::TownAuthority, town);
	});
}

/** Pick 2-3 candidates (one loyal) and announce the elections. */
static void Announce(const Town *t, MetroTownElections &e)
{
	e.announced = true;
	e.candidates.clear();
	uint count = 2 + InteractiveRandomRange(2);
	uint8_t current = MetroAiGetMayor(t);
	uint8_t loyal_character = _metro_election_loyal_characters[InteractiveRandomRange(static_cast<uint32_t>(std::size(_metro_election_loyal_characters)))];
	std::vector<uint8_t> others;
	for (uint8_t c = 0; c < MetroAiMayorCount(); c++) {
		if (c != loyal_character && c != current) others.push_back(c);
	}
	uint loyal_index = InteractiveRandomRange(count);
	for (uint i = 0; i < count; i++) {
		MetroElectionCandidate &cand = e.candidates.emplace_back();
		cand.loyal = i == loyal_index;
		if (cand.loyal || others.empty()) {
			cand.character = loyal_character;
		} else {
			size_t k = InteractiveRandomRange(static_cast<uint32_t>(others.size()));
			cand.character = others[k];
			others.erase(others.begin() + k);
		}
		cand.name = RandomName();
		cand.program = GetString(cand.loyal ? STR_METRO_ELECTION_PROGRAM_LOYAL : _metro_election_programs[cand.character]);
	}
	RequestCandidateTexts(t, e);
	AddTileNewsItem(GetEncodedString(STR_METRO_ELECTION_NEWS_ANNOUNCE, t->index, CalTime::Date{e.next_date}, static_cast<uint>(e.candidates.size())), NewsType::General, t->xy);
	InvalidateWindowData(WindowClass::MetroElections, t->index);
	InvalidateWindowData(WindowClass::TownAuthority, t->index);
}

/** Vote: the winner becomes the mayor, the rating of the company changes. */
static void HoldElections(Town *t, MetroTownElections &e)
{
	if (e.candidates.empty()) Announce(t, e);
	uint chance = MetroElectionsLoyalChance(t, e);
	size_t loyal_index = 0;
	for (size_t i = 0; i < e.candidates.size(); i++) {
		if (e.candidates[i].loyal) loyal_index = i;
	}
	size_t winner = loyal_index;
	uint roll = RandomRange(100);
	if (roll >= chance && e.candidates.size() > 1) {
		/* The others share the rest equally. */
		size_t others = e.candidates.size() - 1;
		size_t k = std::min<size_t>((roll - chance) * others / (100 - chance), others - 1);
		winner = k >= loyal_index ? k + 1 : k;
	}
	const MetroElectionCandidate &w = e.candidates[winner];
	int16_t delta = w.loyal ? METRO_ELECTION_LOYAL_WIN : LoyaltySpec().lose;
	int applied = MetroAiChangeCompanyRating(t, delta);
	MetroAiSetMayor(t, w.character, w.name, w.program);

	MetroElectionResult &r = e.history.emplace_back();
	r.date = Today();
	r.winner = w.name;
	r.character = w.character;
	r.loyal = w.loyal;
	r.delta = static_cast<int16_t>(applied);
	r.chance = static_cast<uint8_t>(chance);

	AddTileNewsItem(GetEncodedString(w.loyal ? STR_METRO_ELECTION_NEWS_LOYAL_WON : STR_METRO_ELECTION_NEWS_OTHER_WON, t->index, w.name, MetroAiMayorName(w.character), applied),
			NewsType::General, t->xy);
	e.announced = false;
	e.candidates.clear();
	e.next_date = YearsLater(e.next_date, METRO_ELECTION_PERIOD_YEARS);
	InvalidateWindowData(WindowClass::MetroElections, t->index);
	InvalidateWindowData(WindowClass::TownAuthority, t->index);
}

static void DailyLoop()
{
	if (!Company::IsValidID(_local_company)) return;
	for (Town *t : Town::Iterate()) {
		if (!MetroCityIsTownActive(t)) continue;
		auto [it, inserted] = _metro_elections.try_emplace(t->index.base());
		MetroTownElections &e = it->second;
		if (inserted) {
			e.town = t->index.base();
			const MetroCityTownState *s = MetroCityGetTownState(t);
			int32_t first = YearsLater(s != nullptr ? s->active_since : Today(), METRO_ELECTION_PERIOD_YEARS);
			int32_t earliest = Today() + METRO_ELECTION_FIRST_MIN_DAYS + InteractiveRandomRange(METRO_ELECTION_FIRST_MAX_DAYS - METRO_ELECTION_FIRST_MIN_DAYS + 1);
			e.next_date = first > Today() + static_cast<int32_t>(METRO_ELECTION_ANNOUNCE_DAYS) ? first : earliest;
		}
		if (t->cache.population < METRO_ELECTION_MIN_POPULATION) {
			/* Too small: the term is extended. */
			if (Today() >= e.next_date) {
				e.next_date = YearsLater(e.next_date, METRO_ELECTION_PERIOD_YEARS);
				e.announced = false;
				e.candidates.clear();
			}
			continue;
		}
		if (!e.announced && Today() >= e.next_date - static_cast<int32_t>(METRO_ELECTION_ANNOUNCE_DAYS)) Announce(t, e);
		if (Today() >= e.next_date) HoldElections(t, e);
	}
}

static const IntervalTimer<TimerGameCalendar> _metro_elections_daily({TimerGameCalendar::Trigger::Day, TimerGameCalendar::Priority::None}, [](auto) {
	DailyLoop();
});

/** Lines for the local authority window: mayor, next elections, candidates. */
std::vector<std::string> MetroElectionsGetAuthorityLines(const Town *t)
{
	std::vector<std::string> lines;
	auto it = _metro_elections.find(t->index.base());
	if (it == _metro_elections.end()) return lines;
	const MetroTownElections &e = it->second;
	if (t->cache.population < METRO_ELECTION_MIN_POPULATION) {
		lines.push_back(GetString(STR_METRO_ELECTION_TOO_SMALL, METRO_ELECTION_MIN_POPULATION));
		return lines;
	}
	lines.push_back(GetString(e.announced ? STR_METRO_ELECTION_SOON : STR_METRO_ELECTION_NEXT, CalTime::Date{e.next_date}, MetroElectionsLoyalChance(t, e)));
	return lines;
}

/* ---------------------------------------------------------------------------------------------- */
/* Window                                                                                         */
/* ---------------------------------------------------------------------------------------------- */

enum MetroElectionsWidgets : WidgetID {
	WID_MEL_CAPTION,
	WID_MEL_INFO,
};

static constexpr std::initializer_list<NWidgetPart> _nested_metro_elections_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Brown),
		NWidget(WWT_CAPTION, Colours::Brown, WID_MEL_CAPTION), SetStringTip(STR_METRO_ELECTION_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Brown),
		NWidget(WWT_STICKYBOX, Colours::Brown),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Brown, WID_MEL_INFO), SetMinimalSize(420, 0), SetFill(1, 0), SetResize(1, 0), EndContainer(),
};

struct MetroElectionsWindow : Window {
	MetroElectionsWindow(WindowDesc &desc, WindowNumber number) : Window(desc)
	{
		this->InitNested(number);
	}

	std::vector<std::string> Lines() const
	{
		std::vector<std::string> lines;
		const Town *t = Town::GetIfValid(this->window_number);
		if (t == nullptr) return lines;
		std::string name = MetroAiGetMayorName(t);
		lines.push_back(name.empty() ? GetString(STR_METRO_ELECTION_MAYOR, MetroAiMayorName(MetroAiGetMayor(t))) : GetString(STR_METRO_ELECTION_MAYOR_NAMED, name, MetroAiMayorName(MetroAiGetMayor(t))));
		auto it = _metro_elections.find(t->index.base());
		if (it == _metro_elections.end()) {
			lines.push_back(GetString(STR_METRO_ELECTION_NOT_YET));
			return lines;
		}
		const MetroTownElections &e = it->second;
		if (t->cache.population < METRO_ELECTION_MIN_POPULATION) {
			lines.push_back(GetString(STR_METRO_ELECTION_TOO_SMALL, METRO_ELECTION_MIN_POPULATION));
		} else {
			lines.push_back(GetString(e.announced ? STR_METRO_ELECTION_SOON : STR_METRO_ELECTION_NEXT, CalTime::Date{e.next_date}, MetroElectionsLoyalChance(t, e)));
		}
		if (e.announced) {
			lines.push_back(GetString(STR_METRO_ELECTION_CANDIDATES));
			for (const MetroElectionCandidate &c : e.candidates) {
				lines.push_back(GetString(c.loyal ? STR_METRO_ELECTION_CANDIDATE_LOYAL : STR_METRO_ELECTION_CANDIDATE, c.name, MetroAiMayorName(c.character), c.program));
			}
			if (e.ai_pending) lines.push_back(GetString(STR_METRO_ELECTION_AI_PENDING));
		}
		if (!e.history.empty()) {
			lines.push_back(GetString(STR_METRO_ELECTION_HISTORY));
			for (auto h = e.history.rbegin(); h != e.history.rend(); ++h) {
				lines.push_back(GetString(STR_METRO_ELECTION_HISTORY_ROW, CalTime::Date{h->date}, h->winner, MetroAiMayorName(h->character), h->loyal ? STR_METRO_ELECTION_LOYAL_MARK : STR_EMPTY, h->chance, h->delta));
			}
		}
		return lines;
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_MEL_CAPTION) return GetString(STR_METRO_ELECTION_CAPTION, this->window_number);
		return this->Window::GetWidgetString(widget, stringid);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget != WID_MEL_INFO) return;
		uint width = std::max<uint>(size.width, ScaleGUITrad(420)) - padding.width;
		uint height = 0;
		for (const std::string &line : this->Lines()) height += GetStringHeight(line, width);
		size.height = std::max<uint>(height, 3 * GetCharacterHeight(FontSize::Normal)) + padding.height;
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_MEL_INFO) return;
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		for (const std::string &line : this->Lines()) ir.top = DrawStringMultiLine(ir, line);
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->ReInit();
	}
};

static WindowDesc _metro_elections_desc(__FILE__, __LINE__,
	WindowPosition::Automatic, "metro_elections", 0, 0,
	WindowClass::MetroElections, WindowClass::TownView,
	{},
	_nested_metro_elections_widgets
);

void ShowMetroElectionsWindow(TownID town)
{
	if (!Town::IsValidID(town)) return;
	AllocateWindowDescFront<MetroElectionsWindow>(_metro_elections_desc, town);
}

/* ---------------------------------------------------------------------------------------------- */
/* Console                                                                                        */
/* ---------------------------------------------------------------------------------------------- */

extern void MetroContractsConsole(std::span<std::string_view> args);

/**
 * Console "metro_civic": contracts [list | offer <town> [type 0-2] | answer <id> <0|1> | check | window];
 * elections [list | announce <town> | vote <town> | advertise <town> | window <town>].
 */
bool ConMetroCivic(std::span<std::string_view> argv)
{
	if (argv.size() < 2) {
		IConsolePrint(CC_HELP, "Metro mod contracts and elections. Usage: metro_civic contracts [offer <town> [type 0-2] | answer <id> <0|1> | check | window] | elections [announce <town> | vote <town> | advertise <town> | window <town>]");
		return true;
	}
	std::span<std::string_view> args = argv.subspan(2);
	if (argv[1] == "contracts") {
		MetroContractsConsole(args);
		return true;
	}
	if (argv[1] != "elections") return false;
	Town *t = args.size() > 1 ? Town::GetIfValid(ParseInteger<uint>(args[1]).value_or(UINT16_MAX)) : nullptr;
	if (t != nullptr) {
		MetroTownElections &e = _metro_elections[t->index.base()];
		e.town = t->index.base();
		if (args[0] == "announce") {
			e.next_date = Today() + METRO_ELECTION_ANNOUNCE_DAYS;
			Announce(t, e);
		} else if (args[0] == "vote") {
			e.next_date = Today();
			HoldElections(t, e);
		} else if (args[0] == "advertise") {
			e.last_advertising = Today();
		} else if (args[0] == "window") {
			ShowMetroElectionsWindow(t->index);
		}
	}
	for (const auto &[town, e] : _metro_elections) {
		const Town *tt = Town::GetIfValid(town);
		if (tt == nullptr) continue;
		IConsolePrint(CC_INFO, "town {} '{}': population {}, rating {}, mayor '{}' ({}), next elections {}, announced {}, loyal chance {}%",
				town, GetString(STR_TOWN_NAME, town), tt->cache.population, tt->ratings[_local_company], MetroAiGetMayorName(tt), GetString(MetroAiMayorName(MetroAiGetMayor(tt))),
				GetString(STR_JUST_DATE_LONG, CalTime::Date{e.next_date}), e.announced, MetroElectionsLoyalChance(tt, e));
		for (const MetroElectionCandidate &c : e.candidates) {
			IConsolePrint(CC_DEFAULT, "  candidate '{}' ({}){}: {}", c.name, GetString(MetroAiMayorName(c.character)), c.loyal ? " loyal" : "", c.program);
		}
		for (const MetroElectionResult &r : e.history) {
			IConsolePrint(CC_DEFAULT, "  {}: '{}' won{}, chance of the loyal one {}%, rating {}", GetString(STR_JUST_DATE_LONG, CalTime::Date{r.date}), r.winner, r.loyal ? " (loyal)" : "", r.chance, r.delta);
		}
	}
	return true;
}
