/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_malls.cpp Metro mod: underground malls (stage 9).
 *
 * A mall belongs to a megahub station (no map tiles): sizes S/M/L cost 1/2/4 times the megahub upgrade of the station.
 * Every month it earns the passengers of the station (boarding and arriving) times 2/4/8, inflated like prices, 50%
 * more at an interchange, and costs 1% of its price. Closing demolishes it with a 25% refund. Numbers:
 * table/metro_mall_tables.h.
 */

#include "stdafx.h"
#include "metro_malls.h"
#include "metro_malls_cmd.h"
#include "metro_stations.h"
#include "metro_overlay.h"
#include "station_base.h"
#include "cargotype.h"
#include "company_base.h"
#include "company_func.h"
#include "command_func.h"
#include "console_func.h"
#include "console_type.h"
#include "economy_func.h"
#include "gfx_func.h"
#include "strings_func.h"
#include "timer/timer.h"
#include "timer/timer_game_calendar.h"
#include "window_func.h"
#include "window_gui.h"
#include "dropdown_type.h"
#include "dropdown_func.h"
#include "table/strings.h"
#include "table/metro_station_tables.h"
#include "table/metro_mall_tables.h"

#include "safeguards.h"

std::map<uint16_t, MetroMall> _metro_malls;

void MetroMallsReset()
{
	_metro_malls.clear();
}

void MetroMallsOnStationDeleted(StationID station)
{
	if (_metro_malls.erase(station.base()) != 0) InvalidateWindowData(WindowClass::MetroMalls, 0);
}

/** Passengers boarding at or arriving to a station with a mall. */
void MetroMallCountPassengers(const Station *st, CargoType cargo, uint amount)
{
	if (_metro_malls.empty() || amount == 0 || !IsCargoInClass(cargo, CargoClass::Passengers)) return;
	auto it = _metro_malls.find(st->index.base());
	if (it != _metro_malls.end()) it->second.flow += amount;
}

/** Price of a mall of the size (1..3) at the station now. */
static Money GetMallPrice(const Station *st, uint8_t size)
{
	return MetroStationLevelPrice(st, METRO_STATION_LEVEL_MEGAHUB) * _metro_mall_specs[size - 1].price_factor;
}

/**
 * Build (size 1..3) or close (size 0) the underground mall of a station.
 * @param flags type of operation
 * @param station the megahub station
 * @param size 1..3 = S, M, L; 0 closes the mall
 */
CommandCost CmdMetroBuildMall(DoCommandFlags flags, StationID station, uint8_t size)
{
	if (!Company::IsValidID(_current_company) || size > std::size(_metro_mall_specs)) return CMD_ERROR;
	const Station *st = Station::GetIfValid(station);
	if (st == nullptr || st->owner != _current_company || !MetroStationIsUnderground(st)) return CommandCost(STR_METRO_STATION_ERR_NOT_STATION);
	auto it = _metro_malls.find(station.base());

	if (size == 0) {
		if (it == _metro_malls.end()) return CommandCost(STR_METRO_MALL_ERR_NO_MALL);
		CommandCost refund(ExpensesType::Construction, -(it->second.price * METRO_MALL_REFUND_PERCENT / 100));
		if (flags.Test(DoCommandFlag::Execute)) {
			_metro_malls.erase(it);
			InvalidateWindowData(WindowClass::MetroMalls, 0);
		}
		return refund;
	}

	if (it != _metro_malls.end()) return CommandCost(STR_METRO_MALL_ERR_HAS_MALL);
	if (GetMetroStationLevel(station) < METRO_STATION_LEVEL_MEGAHUB) return CommandCost(STR_METRO_MALL_ERR_NOT_MEGAHUB);
	Money price = GetMallPrice(st, size);
	if (flags.Test(DoCommandFlag::Execute)) {
		MetroMall &mall = _metro_malls[station.base()];
		mall.size = size;
		mall.price = price;
		InvalidateWindowData(WindowClass::MetroMalls, 0);
	}
	return CommandCost(ExpensesType::Construction, price);
}

/** Monthly: income from the passengers, upkeep, new month of counting. */
static void PayMalls()
{
	for (auto &[station, mall] : _metro_malls) {
		const Station *st = Station::GetIfValid(StationID{station});
		if (st == nullptr || !Company::IsValidID(st->owner)) continue;
		const MetroMallSpec &spec = _metro_mall_specs[mall.size - 1];
		Money income = static_cast<Money>(mall.flow) * spec.income_per_passenger;
		income = income * static_cast<int64_t>(_economy.inflation_prices >> 8) >> 8; // 16-bit fraction.
		if (MetroStationCanBeInterchange(st)) income += income * METRO_MALL_INTERCHANGE_BONUS_PERCENT / 100;
		Money upkeep = mall.price * METRO_MALL_UPKEEP_PERCENT / 100;
		mall.last_flow = mall.flow;
		mall.flow = 0;
		mall.last_income = income;
		mall.last_upkeep = upkeep;
		if (income != 0) SubtractMoneyFromCompany(st->owner, CommandCost(ExpensesType::Other, -income));
		if (upkeep != 0) SubtractMoneyFromCompany(st->owner, CommandCost(ExpensesType::Property, upkeep));
	}
	if (!_metro_malls.empty()) InvalidateWindowData(WindowClass::MetroMalls, 0);
}

static const IntervalTimer<TimerGameCalendar> _metro_malls_monthly({TimerGameCalendar::Trigger::Month, TimerGameCalendar::Priority::None}, [](auto) {
	PayMalls();
});

void MetroMallsConsoleList()
{
	for (const auto &[station, mall] : _metro_malls) {
		IConsolePrint(CC_INFO, "mall at station {} '{}': size {}, price {}, passengers this month {}, last month {}, income {}, upkeep {}", station, GetString(STR_STATION_NAME, station),
				GetString(_metro_mall_specs[mall.size - 1].name), mall.price, mall.flow, mall.last_flow, mall.last_income, mall.last_upkeep);
	}
}

/* ---------------------------------------------------------------------------------------------- */
/* Window: the malls of the company with their income; build and close                            */
/* ---------------------------------------------------------------------------------------------- */

enum MetroMallsWidgets : WidgetID {
	WID_MML_LIST,  ///< Malls and megahubs without a mall.
	WID_MML_SIZE,  ///< Size for building.
	WID_MML_BUILD, ///< Build at the selected megahub.
	WID_MML_CLOSE, ///< Close the selected mall.
};

static constexpr std::initializer_list<NWidgetPart> _nested_metro_malls_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::DarkGreen),
		NWidget(WWT_CAPTION, Colours::DarkGreen), SetStringTip(STR_METRO_MALL_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::DarkGreen),
		NWidget(WWT_STICKYBOX, Colours::DarkGreen),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::DarkGreen, WID_MML_LIST), SetMinimalSize(620, 0), SetFill(1, 1), SetResize(1, 0), SetToolTip(STR_METRO_MALL_LIST_TOOLTIP), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_DROPDOWN, Colours::DarkGreen, WID_MML_SIZE), SetFill(1, 0), SetToolTip(STR_METRO_MALL_SIZE_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::DarkGreen, WID_MML_BUILD), SetFill(1, 0), SetStringTip(STR_METRO_MALL_BUILD, STR_METRO_MALL_BUILD_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::DarkGreen, WID_MML_CLOSE), SetFill(1, 0), SetStringTip(STR_METRO_MALL_CLOSE, STR_METRO_MALL_CLOSE_TOOLTIP),
	EndContainer(),
};

struct MetroMallsWindow : Window {
	std::vector<StationID> rows; ///< Malls of the company, then its megahubs without a mall.
	StationID selected = StationID::Invalid();
	uint8_t size = 1;

	MetroMallsWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->BuildRows();
		this->InitNested(window_number);
	}

	void BuildRows()
	{
		this->rows.clear();
		for (const auto &[station, mall] : _metro_malls) {
			const Station *st = Station::GetIfValid(StationID{station});
			if (st != nullptr && st->owner == _local_company) this->rows.push_back(st->index);
		}
		for (const Station *st : Station::Iterate()) {
			if (st->owner != _local_company || _metro_malls.contains(st->index.base())) continue;
			if (GetMetroStationLevel(st->index) >= METRO_STATION_LEVEL_MEGAHUB) this->rows.push_back(st->index);
		}
		if (std::find(this->rows.begin(), this->rows.end(), this->selected) == this->rows.end()) this->selected = StationID::Invalid();
	}

	std::string RowText(StationID id) const
	{
		const Station *st = Station::Get(id);
		auto it = _metro_malls.find(id.base());
		if (it == _metro_malls.end()) return GetString(STR_METRO_MALL_ROW_EMPTY, id, GetMallPrice(st, this->size));
		const MetroMall &m = it->second;
		return GetString(STR_METRO_MALL_ROW, id, _metro_mall_specs[m.size - 1].name, m.last_flow, m.last_income, m.last_upkeep, m.flow);
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_MML_SIZE) return GetString(_metro_mall_specs[this->size - 1].name);
		return this->Window::GetWidgetString(widget, stringid);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget == WID_MML_LIST) size.height = std::max<uint>(5, static_cast<uint>(this->rows.size()) + 1) * GetCharacterHeight(FontSize::Normal) + padding.height;
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_MML_LIST) return;
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		if (this->rows.empty()) {
			DrawStringMultiLine(ir, STR_METRO_MALL_NONE);
			return;
		}
		for (StationID id : this->rows) {
			if (id == this->selected) GfxFillRect(r.left + 1, ir.top, r.right - 1, ir.top + GetCharacterHeight(FontSize::Normal) - 1, PixelColour{GetColourGradient(Colours::DarkGreen, Shade::Lighter)});
			DrawString(ir, this->RowText(id), TextColour::Black);
			ir.top += GetCharacterHeight(FontSize::Normal);
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_MML_LIST: {
				int row = this->GetRowFromWidget(pt.y, WID_MML_LIST, WidgetDimensions::scaled.framerect.top, GetCharacterHeight(FontSize::Normal));
				if (row >= 0 && row < static_cast<int>(this->rows.size())) {
					this->selected = this->rows[row];
					this->SetDirty();
				}
				break;
			}
			case WID_MML_SIZE: {
				DropDownList list;
				for (uint i = 0; i < std::size(_metro_mall_specs); i++) list.push_back(MakeDropDownListStringItem(_metro_mall_specs[i].name, i + 1));
				ShowDropDownList(this, std::move(list), this->size, WID_MML_SIZE);
				break;
			}
			case WID_MML_BUILD:
				if (this->selected != StationID::Invalid()) Command<Commands::MetroBuildMall>::Post(STR_METRO_MALL_ERR_CANT_BUILD, this->selected, this->size);
				break;
			case WID_MML_CLOSE:
				if (this->selected != StationID::Invalid()) Command<Commands::MetroBuildMall>::Post(STR_METRO_MALL_ERR_CANT_CLOSE, this->selected, 0);
				break;
		}
	}

	void OnDropdownSelect(WidgetID widget, int index, int) override
	{
		if (widget != WID_MML_SIZE) return;
		this->size = static_cast<uint8_t>(index);
		this->SetDirty();
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->BuildRows();
		this->ReInit();
	}
};

static WindowDesc _metro_malls_desc(__FILE__, __LINE__,
	WindowPosition::Automatic, "metro_malls", 0, 0,
	WindowClass::MetroMalls, WindowClass::None,
	{},
	_nested_metro_malls_widgets
);

void ShowMetroMallsWindow()
{
	if (!Company::IsValidID(_local_company)) return;
	AllocateWindowDescFront<MetroMallsWindow>(_metro_malls_desc, 0);
}
