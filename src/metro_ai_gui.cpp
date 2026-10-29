/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_ai_gui.cpp Metro mod: "Results of the year" window. */

#include "stdafx.h"
#include "metro_ai.h"
#include "gfx_func.h"
#include "strings_func.h"
#include "window_func.h"
#include "window_gui.h"
#include "table/strings.h"

#include "safeguards.h"

/** Widgets of the #MetroYearReportWindow. */
enum MetroYearReportWidgets : WidgetID {
	WID_MYR_CAPTION, ///< Caption with the year.
	WID_MYR_TEXT,    ///< Article.
	WID_MYR_PREV,    ///< Previous year.
	WID_MYR_NEXT,    ///< Next year.
};

static constexpr std::initializer_list<NWidgetPart> _nested_metro_year_report_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_MYR_CAPTION),
		NWidget(WWT_SHADEBOX, Colours::Grey),
		NWidget(WWT_STICKYBOX, Colours::Grey),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey, WID_MYR_TEXT), SetMinimalSize(420, 60), SetFill(1, 1), EndContainer(),
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MYR_PREV), SetFill(1, 0), SetStringTip(STR_METRO_YEAR_REPORT_PREV, STR_METRO_YEAR_REPORT_PREV_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MYR_NEXT), SetFill(1, 0), SetStringTip(STR_METRO_YEAR_REPORT_NEXT, STR_METRO_YEAR_REPORT_NEXT_TOOLTIP),
	EndContainer(),
};

/** Window with the "Results of the year" newspaper article written by the AI. */
struct MetroYearReportWindow : Window {
	int32_t year; ///< Year shown.

	MetroYearReportWindow(WindowDesc &desc, int32_t year) : Window(desc), year(year)
	{
		this->InitNested(0);
		this->UpdateButtons();
	}

	/** Year of the nearest report before (@p dir < 0) or after (@p dir > 0) the shown one, or -1. */
	int32_t FindNeighbour(int dir) const
	{
		int32_t best = -1;
		for (const MetroYearReport &report : _metro_year_reports) {
			if (dir < 0 && report.year < this->year && (best == -1 || report.year > best)) best = report.year;
			if (dir > 0 && report.year > this->year && (best == -1 || report.year < best)) best = report.year;
		}
		return best;
	}

	void UpdateButtons()
	{
		this->SetWidgetDisabledState(WID_MYR_PREV, this->FindNeighbour(-1) == -1);
		this->SetWidgetDisabledState(WID_MYR_NEXT, this->FindNeighbour(1) == -1);
	}

	std::pair<std::string, std::string> GetTexts() const
	{
		const MetroYearReport *report = MetroAiGetYearReport(this->year);
		if (report == nullptr) {
			std::string reason;
			if (MetroAiGetPendingYearReport() == this->year) return {std::string{}, GetString(STR_METRO_YEAR_REPORT_PENDING)};
			if (MetroAiGetFailedYearReport(reason) == this->year) return {std::string{}, GetString(STR_METRO_YEAR_REPORT_FAILED, reason)};
			return {std::string{}, GetString(STR_METRO_YEAR_REPORT_NONE)};
		}
		return {report->title.empty() ? std::string{} : GetString(STR_METRO_YEAR_REPORT_TITLE, report->title), GetString(STR_METRO_YEAR_REPORT_TEXT, report->text)};
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_MYR_CAPTION) return GetString(STR_METRO_YEAR_REPORT_CAPTION, this->year);
		return this->Window::GetWidgetString(widget, stringid);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget != WID_MYR_TEXT) return;
		int width = size.width - padding.width - WidgetDimensions::scaled.framerect.Horizontal();
		auto [title, text] = this->GetTexts();
		uint height = GetStringHeight(text, width);
		if (!title.empty()) height += GetStringHeight(title, width) + WidgetDimensions::scaled.vsep_wide;
		size.height = std::max<uint>(size.height, height + padding.height + WidgetDimensions::scaled.framerect.Vertical());
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_MYR_TEXT) return;
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		auto [title, text] = this->GetTexts();
		if (!title.empty()) ir.top = DrawStringMultiLine(ir, title) + WidgetDimensions::scaled.vsep_wide;
		DrawStringMultiLine(ir, text);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget != WID_MYR_PREV && widget != WID_MYR_NEXT) return;
		int32_t year = this->FindNeighbour(widget == WID_MYR_PREV ? -1 : 1);
		if (year == -1) return;
		this->year = year;
		this->UpdateButtons();
		this->ReInit();
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->UpdateButtons();
		this->ReInit();
	}
};

static WindowDesc _metro_year_report_desc(__FILE__, __LINE__,
	WindowPosition::Center, "metro_year_report", 0, 0,
	WindowClass::MetroYearReport, WindowClass::None,
	{},
	_nested_metro_year_report_widgets
);

/**
 * Show the "Results of the year" window.
 * @param year Year to show; -1 for the latest article (or the last year when there is none).
 */
void ShowMetroYearReportWindow(int32_t year)
{
	if (year == -1) {
		/* The newest article, or the one being written, or the one that failed. */
		std::string reason;
		for (const MetroYearReport &report : _metro_year_reports) year = std::max(year, report.year);
		year = std::max({year, MetroAiGetPendingYearReport(), MetroAiGetFailedYearReport(reason)});
		if (year == -1) year = CalTime::CurYear().base() - 1;
	}
	CloseWindowById(WindowClass::MetroYearReport, 0);
	new MetroYearReportWindow(_metro_year_report_desc, year);
}
