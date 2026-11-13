/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_contracts.cpp Metro mod: government contracts (stage 9, decisions of the user).
 *
 * Active towns of 5000 people and more offer the player a contract every 1..2 years: an underground station near the
 * centre of a district (1 year), a line to a neighbour town within 100 tiles (2 years) or X passengers a month three
 * months in a row, X = 10% of the population (1 year). The player has 30 days to accept or refuse (no penalty). In
 * time: money (2/4/3 yearly upkeeps of a megahub) and +150 rating; a failure costs 150 rating and the next offer comes
 * only in 2..4 years. The game sets the conditions and the reward; the AI mayor only writes the letter (a template
 * without the AI). Numbers: table/metro_civic_tables.h.
 */

#include "stdafx.h"
#include "metro_civic.h"
#include "metro_contracts_cmd.h"
#include "metro_ai.h"
#include "metro_city.h"
#include "metro_stations.h"
#include "metro_town.h"
#include "metro_underground.h"
#include "cargotype.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "console_func.h"
#include "console_type.h"
#include "core/random_func.hpp"
#include "core/string_consumer.hpp"
#include "economy_func.h"
#include "gfx_func.h"
#include "map_func.h"
#include "news_func.h"
#include "order_base.h"
#include "station_base.h"
#include "strings_func.h"
#include "timer/timer.h"
#include "timer/timer_game_calendar.h"
#include "town.h"
#include "vehicle_base.h"
#include "viewport_func.h"
#include "window_func.h"
#include "window_gui.h"
#include "table/strings.h"
#include "table/metro_station_tables.h"
#include "table/metro_civic_tables.h"
#include "3rdparty/nlohmann/json.hpp"

#include "safeguards.h"

std::vector<MetroContract> _metro_contracts;
std::map<uint16_t, int32_t> _metro_contract_next_offer;

void MetroContractsReset()
{
	_metro_contracts.clear();
	_metro_contract_next_offer.clear();
}

void MetroContractsOnTownDeleted(TownID town)
{
	std::erase_if(_metro_contracts, [&](const MetroContract &c) { return c.town == town.base() || (c.type == to_underlying(MetroContractType::NeighbourLine) && c.target == town.base()); });
	_metro_contract_next_offer.erase(town.base());
}

static MetroContract *FindContract(uint32_t id)
{
	for (MetroContract &c : _metro_contracts) {
		if (c.id == id) return &c;
	}
	return nullptr;
}

static int32_t Today()
{
	return CalTime::CurDate().base();
}

/** Yearly upkeep of a megahub: the reward unit. */
static Money MegahubYearUpkeep()
{
	Money megahub = MetroGetUndergroundPiecePrice(1) * METRO_CONTRACT_REF_PLATFORMS * _metro_station_levels_spec[METRO_STATION_LEVEL_MEGAHUB].cost_factor;
	return megahub * METRO_CONTRACT_YEAR_UPKEEP_PERCENT / 100;
}

/* ---------------------------------------------------------------------------------------------- */
/* Conditions                                                                                     */
/* ---------------------------------------------------------------------------------------------- */

/** Whether the company has an underground station near the tile. */
static bool HasMetroStationNear(TileIndex centre)
{
	for (const Station *st : Station::Iterate()) {
		if (st->owner != _local_company || !MetroStationIsUnderground(st)) continue;
		if (DistanceMax(st->xy, centre) <= METRO_CONTRACT_DISTRICT_RADIUS) return true;
	}
	return false;
}

/** Whether a vehicle of the company serves both towns in its orders. */
static bool HasLineBetween(uint16_t a, uint16_t b)
{
	for (const Vehicle *v : Vehicle::Iterate()) {
		if (!v->IsPrimaryVehicle() || v->owner != _local_company) continue;
		bool seen_a = false, seen_b = false;
		for (VehicleOrderID i = 0; i < v->GetNumOrders(); i++) {
			const Order *o = v->GetOrder(i);
			if (o == nullptr || !o->IsType(OT_GOTO_STATION)) continue;
			const Station *st = Station::GetIfValid(o->GetDestination().ToStationID());
			if (st == nullptr || st->town == nullptr) continue;
			if (st->town->index.base() == a) seen_a = true;
			if (st->town->index.base() == b) seen_b = true;
		}
		if (seen_a && seen_b) return true;
	}
	return false;
}

/** Passengers carried from the town last month (all its passenger cargoes). */
static uint32_t TownPassengersLastMonth(const Town *t)
{
	uint32_t transported = 0;
	for (CargoType cargo : CargoSpec::town_production_cargo_mask[TownProductionEffect::Passengers]) {
		auto it = t->GetCargoSupplied(cargo);
		if (it != std::end(t->supplied)) transported += it->history[LAST_MONTH].transported;
	}
	return transported;
}

/** Nearest other town within the distance not yet linked to @p t, or nullptr. */
static const Town *FindNeighbourWithoutLine(const Town *t)
{
	const Town *best = nullptr;
	uint best_dist = UINT_MAX;
	for (const Town *o : Town::Iterate()) {
		if (o == t) continue;
		uint d = DistanceManhattan(o->xy, t->xy);
		if (d > METRO_CONTRACT_NEIGHBOUR_DISTANCE || d >= best_dist) continue;
		if (HasLineBetween(t->index.base(), o->index.base())) continue;
		best = o;
		best_dist = d;
	}
	return best;
}

/** A district of the town without a metro station of the company near its centre, or INVALID_TILE. */
static TileIndex FindDistrictWithoutMetro(const Town *t)
{
	if (!IsMetroLayerAvailable(1)) return INVALID_TILE;
	std::vector<TileIndex> centres;
	for (const MetroDistrict &d : _metro_districts) {
		if (d.town == t->index.base() && !HasMetroStationNear(TileIndex{d.tile})) centres.push_back(TileIndex{d.tile});
	}
	if (centres.empty()) return INVALID_TILE;
	return centres[RandomRange(static_cast<uint32_t>(centres.size()))];
}

/* ---------------------------------------------------------------------------------------------- */
/* Texts                                                                                          */
/* ---------------------------------------------------------------------------------------------- */

/** What the contract asks, one line. */
std::string MetroContractSummary(const MetroContract &c)
{
	switch (static_cast<MetroContractType>(c.type)) {
		case MetroContractType::DistrictStation: {
			const Town *t = Town::GetIfValid(c.town);
			int type = t != nullptr ? GetMetroDistrictTypeAt(t, TileIndex{c.target}) : -1;
			StringID district = type == to_underlying(MetroDistrictType::Business) ? STR_METRO_HEAT_DISTRICT_BUSINESS : STR_METRO_HEAT_DISTRICT_RESIDENTIAL;
			return GetString(STR_METRO_CONTRACT_SUMMARY_DISTRICT, district, TileX(TileIndex{c.target}), TileY(TileIndex{c.target}));
		}
		case MetroContractType::NeighbourLine:
			return GetString(STR_METRO_CONTRACT_SUMMARY_LINE, c.target);
		case MetroContractType::Passengers:
			return GetString(STR_METRO_CONTRACT_SUMMARY_PASSENGERS, c.target, METRO_CONTRACT_PASSENGER_MONTHS);
		default:
			return {};
	}
}

/** State, deadline and progress, one line. */
std::string MetroContractStatus(const MetroContract &c)
{
	switch (static_cast<MetroContractState>(c.state)) {
		case MetroContractState::Offered: return GetString(STR_METRO_CONTRACT_STATUS_OFFERED, CalTime::Date{c.deadline});
		case MetroContractState::Active:
			if (c.type == to_underlying(MetroContractType::Passengers)) {
				return GetString(STR_METRO_CONTRACT_STATUS_ACTIVE_PASSENGERS, CalTime::Date{c.deadline}, c.last_value, c.progress, METRO_CONTRACT_PASSENGER_MONTHS);
			}
			return GetString(STR_METRO_CONTRACT_STATUS_ACTIVE, CalTime::Date{c.deadline});
		case MetroContractState::Done: return GetString(STR_METRO_CONTRACT_STATUS_DONE, CalTime::Date{c.end_date});
		case MetroContractState::Failed: return GetString(STR_METRO_CONTRACT_STATUS_FAILED, CalTime::Date{c.end_date});
		case MetroContractState::Refused: return GetString(STR_METRO_CONTRACT_STATUS_REFUSED, CalTime::Date{c.end_date});
		case MetroContractState::Expired: return GetString(STR_METRO_CONTRACT_STATUS_EXPIRED, CalTime::Date{c.end_date});
	}
	return {};
}

/** The letter without the AI. */
static void SetTemplateText(MetroContract &c)
{
	c.title = GetString(STR_METRO_CONTRACT_TITLE, c.town);
	c.text = GetString(STR_METRO_CONTRACT_TEMPLATE, c.town, MetroContractSummary(c), _metro_contract_specs[c.type].deadline_days / 365, c.reward, c.rating);
}

/** Ask the AI mayor for the letter; the template stays if it fails. */
static void RequestContractText(MetroContract &c)
{
	if (!MetroAiIsEnabled()) return;
	const Town *t = Town::GetIfValid(c.town);
	if (t == nullptr) return;
	c.text_pending = true;
	std::string system = "You are the mayor of a town in the transport tycoon game OpenTTD. Character: " + std::string(MetroAiMayorPrompt(MetroAiGetMayor(t))) + ". "
		"You offer a government contract to the transport company of the player. Answer with one JSON object only, no markdown: "
		"{\"title\": a headline of at most 8 words, \"text\": 2-3 sentences of your letter in first person}. "
		"State the task, the deadline and the reward exactly as given, do not change or add numbers or conditions. "
		"Write in " + MetroAiLanguageName() + ".";
	std::string user = fmt::format("town: {}; population: {}\ntask: {}\nconcrete task: {}\ndeadline: {} year(s) after accepting\nreward: {} and +{} to the rating of the company\n",
			GetString(STR_TOWN_NAME, t->index), t->cache.population, _metro_contract_specs[c.type].prompt, MetroContractSummary(c),
			_metro_contract_specs[c.type].deadline_days / 365, GetString(STR_JUST_CURRENCY_LONG, c.reward), c.rating);
	uint32_t id = c.id;
	MetroAiRequestJson(std::move(system), std::move(user), 600, [id](const std::string *json) {
		MetroContract *c = FindContract(id);
		if (c == nullptr) return;
		c->text_pending = false;
		if (json != nullptr) {
			nlohmann::json j = nlohmann::json::parse(*json, nullptr, false);
			if (j.is_object() && j.contains("title") && j.contains("text") && j["title"].is_string() && j["text"].is_string()) {
				std::string title = j["title"].get<std::string>();
				std::string text = j["text"].get<std::string>();
				if (!title.empty() && !text.empty() && title.size() < 200 && text.size() < 1500) {
					c->title = std::move(title);
					c->text = std::move(text);
				}
			}
		}
		InvalidateWindowClassesData(WindowClass::MetroContracts);
	});
}

/* ---------------------------------------------------------------------------------------------- */
/* Offers and results                                                                             */
/* ---------------------------------------------------------------------------------------------- */

static uint32_t NextContractId()
{
	uint32_t id = 1;
	for (const MetroContract &c : _metro_contracts) id = std::max(id, c.id + 1);
	return id;
}

static bool HasOpenContract(const Town *t)
{
	for (const MetroContract &c : _metro_contracts) {
		if (c.town == t->index.base() && (c.state == to_underlying(MetroContractState::Offered) || c.state == to_underlying(MetroContractState::Active))) return true;
	}
	return false;
}

/** Make an offer of the town; false if it has nothing to offer. */
static bool MakeOffer(const Town *t, int forced_type = -1)
{
	std::vector<std::pair<MetroContractType, uint32_t>> options;
	TileIndex district = FindDistrictWithoutMetro(t);
	if (district != INVALID_TILE) options.emplace_back(MetroContractType::DistrictStation, district.base());
	const Town *neighbour = FindNeighbourWithoutLine(t);
	if (neighbour != nullptr) options.emplace_back(MetroContractType::NeighbourLine, neighbour->index.base());
	uint32_t x = std::max<uint32_t>(10, t->cache.population * METRO_CONTRACT_PASSENGER_PERCENT / 100 / 10 * 10);
	options.emplace_back(MetroContractType::Passengers, x);
	if (forced_type >= 0) std::erase_if(options, [&](const auto &o) { return to_underlying(o.first) != forced_type; });
	if (options.empty()) return false;

	const auto &[type, target] = options[RandomRange(static_cast<uint32_t>(options.size()))];
	MetroContract &c = _metro_contracts.emplace_back();
	c.id = NextContractId();
	c.town = t->index.base();
	c.type = to_underlying(type);
	c.state = to_underlying(MetroContractState::Offered);
	c.offer_date = Today();
	c.deadline = Today() + METRO_CONTRACT_ANSWER_DAYS;
	c.target = target;
	c.reward = (MegahubYearUpkeep() * _metro_contract_specs[c.type].reward_years).base();
	c.rating = METRO_CONTRACT_RATING;
	SetTemplateText(c);
	RequestContractText(c);

	AddTileNewsItem(GetEncodedString(STR_METRO_CONTRACT_NEWS_OFFER, t->index, MetroContractSummary(c), c.reward), NewsType::General, t->xy);
	InvalidateWindowClassesData(WindowClass::MetroContracts);
	ShowMetroContractsWindow(t->index);
	return true;
}

/** Change the rating of the company in the town. */
static void ChangeRating(uint16_t town, int delta)
{
	Town *t = Town::GetIfValid(town);
	if (t != nullptr && delta != 0) MetroAiChangeCompanyRating(t, delta);
}

static void Finish(MetroContract &c, MetroContractState state)
{
	c.state = to_underlying(state);
	c.end_date = Today();
	const Town *t = Town::GetIfValid(c.town);
	if (state == MetroContractState::Done) {
		if (Company::IsValidID(_local_company)) SubtractMoneyFromCompany(_local_company, CommandCost(ExpensesType::Other, -c.reward));
		ChangeRating(c.town, c.rating);
		if (t != nullptr) AddTileNewsItem(GetEncodedString(STR_METRO_CONTRACT_NEWS_DONE, t->index, c.reward, c.rating), NewsType::General, t->xy);
		_metro_contract_next_offer[c.town] = Today() + METRO_CONTRACT_OFFER_MIN_DAYS + RandomRange(METRO_CONTRACT_OFFER_MAX_DAYS - METRO_CONTRACT_OFFER_MIN_DAYS + 1);
	} else if (state == MetroContractState::Failed) {
		ChangeRating(c.town, -c.rating);
		if (t != nullptr) AddTileNewsItem(GetEncodedString(STR_METRO_CONTRACT_NEWS_FAILED, t->index, c.rating), NewsType::General, t->xy);
		_metro_contract_next_offer[c.town] = Today() + METRO_CONTRACT_FAIL_MIN_DAYS + RandomRange(METRO_CONTRACT_FAIL_MAX_DAYS - METRO_CONTRACT_FAIL_MIN_DAYS + 1);
	}
	InvalidateWindowClassesData(WindowClass::MetroContracts);
}

/** Check an active contract; @p month_start: the monthly passenger count is due. */
static void CheckContract(MetroContract &c, bool month_start)
{
	const Town *t = Town::GetIfValid(c.town);
	if (t == nullptr) return;
	bool done = false;
	switch (static_cast<MetroContractType>(c.type)) {
		case MetroContractType::DistrictStation: done = HasMetroStationNear(TileIndex{c.target}); break;
		case MetroContractType::NeighbourLine: done = Town::IsValidID(c.target) && HasLineBetween(c.town, static_cast<uint16_t>(c.target)); break;
		case MetroContractType::Passengers:
			if (!month_start) break;
			c.last_value = TownPassengersLastMonth(t);
			c.progress = c.last_value >= c.target ? c.progress + 1 : 0;
			done = c.progress >= METRO_CONTRACT_PASSENGER_MONTHS;
			InvalidateWindowClassesData(WindowClass::MetroContracts);
			break;
		default: break;
	}
	if (done) {
		Finish(c, MetroContractState::Done);
	} else if (Today() > c.deadline) {
		Finish(c, MetroContractState::Failed);
	}
}

static void DailyLoop()
{
	if (!Company::IsValidID(_local_company)) return;
	bool month_start = CalTime::CurDay() == 1;
	for (MetroContract &c : _metro_contracts) {
		if (c.state == to_underlying(MetroContractState::Offered) && Today() > c.deadline) {
			c.state = to_underlying(MetroContractState::Expired);
			c.end_date = Today();
			InvalidateWindowClassesData(WindowClass::MetroContracts);
		} else if (c.state == to_underlying(MetroContractState::Active)) {
			CheckContract(c, month_start);
		}
	}
	for (const Town *t : Town::Iterate()) {
		if (!MetroCityIsTownActive(t) || t->cache.population < METRO_CONTRACT_MIN_POPULATION) continue;
		auto [it, inserted] = _metro_contract_next_offer.try_emplace(t->index.base(), 0);
		if (inserted) it->second = Today() + METRO_CONTRACT_OFFER_MIN_DAYS + RandomRange(METRO_CONTRACT_OFFER_MAX_DAYS - METRO_CONTRACT_OFFER_MIN_DAYS + 1);
		if (Today() < it->second || HasOpenContract(t)) continue;
		it->second = Today() + METRO_CONTRACT_OFFER_MIN_DAYS + RandomRange(METRO_CONTRACT_OFFER_MAX_DAYS - METRO_CONTRACT_OFFER_MIN_DAYS + 1);
		MakeOffer(t);
	}
}

static const IntervalTimer<TimerGameCalendar> _metro_contracts_daily({TimerGameCalendar::Trigger::Day, TimerGameCalendar::Priority::None}, [](auto) {
	DailyLoop();
});

/**
 * Accept or refuse an offered contract.
 * @param flags Command flags.
 * @param contract Number of the contract.
 * @param accept Accept (true) or refuse.
 */
CommandCost CmdMetroContractAnswer(DoCommandFlags flags, uint32_t contract, bool accept)
{
	MetroContract *c = FindContract(contract);
	if (c == nullptr || c->state != to_underlying(MetroContractState::Offered) || _current_company != _local_company) return CMD_ERROR;
	if (flags.Test(DoCommandFlag::Execute)) {
		if (accept) {
			c->state = to_underlying(MetroContractState::Active);
			c->deadline = Today() + _metro_contract_specs[c->type].deadline_days;
			c->progress = 0;
			CheckContract(*c, false); // Already fulfilled (a station built meanwhile): paid at once.
		} else {
			c->state = to_underlying(MetroContractState::Refused);
			c->end_date = Today();
		}
		InvalidateWindowClassesData(WindowClass::MetroContracts);
	}
	return CommandCost();
}

/* ---------------------------------------------------------------------------------------------- */
/* Window                                                                                         */
/* ---------------------------------------------------------------------------------------------- */

enum MetroContractsWidgets : WidgetID {
	WID_MCT_LIST,    ///< Contracts.
	WID_MCT_SCROLL,  ///< Scrollbar of the list.
	WID_MCT_DETAILS, ///< Letter of the selected contract.
	WID_MCT_ACCEPT,
	WID_MCT_REFUSE,
	WID_MCT_TOWN,    ///< Scroll to the town.
};

static constexpr std::initializer_list<NWidgetPart> _nested_metro_contracts_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Brown),
		NWidget(WWT_CAPTION, Colours::Brown), SetStringTip(STR_METRO_CONTRACT_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Brown),
		NWidget(WWT_STICKYBOX, Colours::Brown),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Brown, WID_MCT_LIST), SetMinimalSize(560, 0), SetFill(1, 0), SetResize(1, 0), SetScrollbar(WID_MCT_SCROLL), SetToolTip(STR_METRO_CONTRACT_LIST_TOOLTIP), EndContainer(),
		NWidget(NWID_VSCROLLBAR, Colours::Brown, WID_MCT_SCROLL),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Brown, WID_MCT_DETAILS), SetMinimalSize(560, 0), SetFill(1, 0), SetResize(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MCT_ACCEPT), SetFill(1, 0), SetStringTip(STR_METRO_CONTRACT_ACCEPT, STR_METRO_CONTRACT_ACCEPT_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MCT_REFUSE), SetFill(1, 0), SetStringTip(STR_METRO_CONTRACT_REFUSE, STR_METRO_CONTRACT_REFUSE_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MCT_TOWN), SetFill(1, 0), SetStringTip(STR_METRO_CONTRACT_TOWN, STR_METRO_CONTRACT_TOWN_TOOLTIP),
	EndContainer(),
};

static constexpr uint METRO_CONTRACT_LIST_ROWS = 6;
static constexpr uint METRO_CONTRACT_DETAIL_LINES = 7;

struct MetroContractsWindow : Window {
	std::vector<uint32_t> rows; ///< Offered and active first, then the history (newest first).
	uint32_t selected = 0;
	Scrollbar *vscroll = nullptr;

	MetroContractsWindow(WindowDesc &desc, WindowNumber number, TownID town) : Window(desc)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_MCT_SCROLL);
		this->FinishInitNested(number);
		this->BuildRows();
		this->SelectTown(town);
	}

	void BuildRows()
	{
		this->rows.clear();
		for (const MetroContract &c : _metro_contracts) {
			if (c.state <= to_underlying(MetroContractState::Active)) this->rows.push_back(c.id);
		}
		std::sort(this->rows.begin(), this->rows.end(), [](uint32_t a, uint32_t b) { return FindContract(a)->state < FindContract(b)->state; });
		for (auto it = _metro_contracts.rbegin(); it != _metro_contracts.rend(); ++it) {
			if (it->state > to_underlying(MetroContractState::Active)) this->rows.push_back(it->id);
		}
		this->vscroll->SetCount(this->rows.size());
		if (FindContract(this->selected) == nullptr) this->selected = this->rows.empty() ? 0 : this->rows.front();
	}

	void SelectTown(TownID town)
	{
		for (uint32_t id : this->rows) {
			if (FindContract(id)->town == town.base()) {
				this->selected = id;
				break;
			}
		}
		this->SetDirty();
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, [[maybe_unused]] Dimension &fill, Dimension &resize) override
	{
		if (widget == WID_MCT_LIST) {
			resize.height = GetCharacterHeight(FontSize::Normal);
			size.height = METRO_CONTRACT_LIST_ROWS * resize.height + padding.height;
		}
		if (widget == WID_MCT_DETAILS) size.height = METRO_CONTRACT_DETAIL_LINES * GetCharacterHeight(FontSize::Normal) + padding.height;
	}

	void OnPaint() override
	{
		const MetroContract *c = FindContract(this->selected);
		bool offered = c != nullptr && c->state == to_underlying(MetroContractState::Offered);
		this->SetWidgetDisabledState(WID_MCT_ACCEPT, !offered);
		this->SetWidgetDisabledState(WID_MCT_REFUSE, !offered);
		this->SetWidgetDisabledState(WID_MCT_TOWN, c == nullptr);
		this->DrawWidgets();
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		int line = GetCharacterHeight(FontSize::Normal);
		if (widget == WID_MCT_LIST) {
			if (this->rows.empty()) {
				DrawString(ir, STR_METRO_CONTRACT_NONE);
				return;
			}
			auto [first, last] = this->vscroll->GetVisibleRangeIterators(this->rows);
			for (auto it = first; it != last; ++it) {
				const MetroContract *c = FindContract(*it);
				if (*it == this->selected) GfxFillRect(r.left + 1, ir.top, r.right - 1, ir.top + line - 1, PixelColour{GetColourGradient(Colours::Brown, Shade::Lighter)});
				DrawString(ir, GetString(STR_METRO_CONTRACT_ROW, c->town, MetroContractSummary(*c), MetroContractStatus(*c)));
				ir.top += line;
			}
		}
		if (widget == WID_MCT_DETAILS) {
			const MetroContract *c = FindContract(this->selected);
			if (c == nullptr) return;
			ir.top = DrawString(ir, GetString(STR_METRO_CONTRACT_DETAIL_TITLE, c->title));
			ir.top = DrawStringMultiLine(ir, GetString(STR_METRO_CONTRACT_DETAIL_TEXT, c->text));
			ir.top += line / 2;
			ir.top = DrawString(ir, GetString(STR_METRO_CONTRACT_DETAIL_REWARD, c->reward, c->rating, c->rating));
			DrawString(ir, MetroContractStatus(*c));
			if (c->text_pending) DrawString(ir.left, ir.right, r.bottom - line - WidgetDimensions::scaled.framerect.bottom, STR_METRO_CONTRACT_PENDING, TextColour::Grey, AlignmentH::End);
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_MCT_LIST: {
				auto it = this->vscroll->GetScrolledItemFromWidget(this->rows, pt.y, this, WID_MCT_LIST, WidgetDimensions::scaled.framerect.top);
				if (it != this->rows.end()) {
					this->selected = *it;
					this->SetDirty();
				}
				break;
			}
			case WID_MCT_ACCEPT:
			case WID_MCT_REFUSE:
				Command<Commands::MetroContractAnswer>::Post(STR_METRO_CONTRACT_ERR_ANSWER, this->selected, widget == WID_MCT_ACCEPT);
				break;
			case WID_MCT_TOWN: {
				const MetroContract *c = FindContract(this->selected);
				const Town *t = c != nullptr ? Town::GetIfValid(c->town) : nullptr;
				if (t != nullptr) ScrollMainWindowToTile(c->type == to_underlying(MetroContractType::DistrictStation) ? TileIndex{c->target} : t->xy);
				break;
			}
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_MCT_LIST, WidgetDimensions::scaled.framerect.Vertical());
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->BuildRows();
		this->SetDirty();
	}
};

static WindowDesc _metro_contracts_desc(__FILE__, __LINE__,
	WindowPosition::Automatic, "metro_contracts", 0, 0,
	WindowClass::MetroContracts, WindowClass::None,
	{},
	_nested_metro_contracts_widgets
);

void ShowMetroContractsWindow(TownID town)
{
	if (!Company::IsValidID(_local_company)) return;
	MetroContractsWindow *w = dynamic_cast<MetroContractsWindow *>(BringWindowToFrontById(WindowClass::MetroContracts, 0));
	if (w != nullptr) {
		w->BuildRows();
		w->SelectTown(town);
		return;
	}
	new MetroContractsWindow(_metro_contracts_desc, 0, town);
}

/* ---------------------------------------------------------------------------------------------- */
/* Console                                                                                        */
/* ---------------------------------------------------------------------------------------------- */

/** Console part of "metro_civic": contracts list | offer <town> [type] | answer <id> <0|1> | check | window. */
void MetroContractsConsole(std::span<std::string_view> args)
{
	if (!args.empty() && args[0] == "offer" && args.size() > 1) {
		const Town *t = Town::GetIfValid(ParseInteger<uint>(args[1]).value_or(UINT16_MAX));
		int type = args.size() > 2 ? static_cast<int>(ParseInteger<uint>(args[2]).value_or(0)) : -1;
		IConsolePrint(CC_INFO, "offer: {}", t != nullptr && MakeOffer(t, type) ? "made" : "nothing to offer");
	} else if (!args.empty() && args[0] == "answer" && args.size() > 2) {
		CommandCost ret = Command<Commands::MetroContractAnswer>::Do(DoCommandFlag::Execute, ParseInteger<uint>(args[1]).value_or(0), ParseInteger<uint>(args[2]).value_or(0) != 0);
		IConsolePrint(CC_INFO, "answer: {}", ret.Succeeded() ? "done" : "failed");
	} else if (!args.empty() && args[0] == "check") {
		/* Run the monthly check at once (tests). */
		for (MetroContract &c : _metro_contracts) {
			if (c.state == to_underlying(MetroContractState::Active)) CheckContract(c, true);
		}
	} else if (!args.empty() && args[0] == "window") {
		ShowMetroContractsWindow();
	}
	IConsolePrint(CC_INFO, "contracts: {}, reward unit (megahub upkeep a year) {}", _metro_contracts.size(), GetString(STR_JUST_CURRENCY_LONG, MegahubYearUpkeep()));
	for (const MetroContract &c : _metro_contracts) {
		IConsolePrint(CC_DEFAULT, "#{} town {} '{}': {} — {}; reward {}, rating {}; '{}': {}", c.id, c.town, GetString(STR_TOWN_NAME, c.town), MetroContractSummary(c), MetroContractStatus(c),
				GetString(STR_JUST_CURRENCY_LONG, c.reward), c.rating, c.title, c.text);
	}
	for (const auto &[town, date] : _metro_contract_next_offer) {
		IConsolePrint(CC_DEFAULT, "town {} next offer {}", town, GetString(STR_JUST_DATE_LONG, CalTime::Date{date}));
	}
}
