/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_city_gui.cpp Metro mod: town services window and the window of a service building. */

#include "stdafx.h"
#include "metro_city.h"
#include "metro_city_cmd.h"
#include "command_func.h"
#include "gfx_func.h"
#include "gui.h"
#include "landscape_cmd.h"
#include "strings_func.h"
#include "tilehighlight_func.h"
#include "town.h"
#include "viewport_func.h"
#include "window_func.h"
#include "window_gui.h"
#include "zoom_func.h"
#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

/* ---------------------------------------------------------------------------------------------- */
/* Services of a town                                                                             */
/* ---------------------------------------------------------------------------------------------- */

/** Widgets of the #MetroTownServicesWindow. */
enum MetroTownServicesWidgets : WidgetID {
	WID_MTS_CAPTION, ///< Caption with the town name.
	WID_MTS_LIST,    ///< List of services.
	WID_MTS_INFO,    ///< Hint or why the town has no services.
};

static constexpr std::initializer_list<NWidgetPart> _nested_metro_town_services_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Brown),
		NWidget(WWT_CAPTION, Colours::Brown, WID_MTS_CAPTION),
		NWidget(WWT_SHADEBOX, Colours::Brown),
		NWidget(WWT_STICKYBOX, Colours::Brown),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Brown, WID_MTS_LIST), SetMinimalSize(320, 0), SetFill(1, 0), SetResize(1, 0), SetToolTip(STR_METRO_CITY_SERVICES_LIST_TOOLTIP), EndContainer(),
	NWidget(WWT_PANEL, Colours::Brown, WID_MTS_INFO), SetMinimalSize(320, 0), SetFill(1, 1), SetResize(1, 0), EndContainer(),
};

/** List of the services of a town; click a row, then a tile, to build. */
struct MetroTownServicesWindow : Window {
	MetroServiceType selected = MetroServiceType::End; ///< Service being placed.

	MetroTownServicesWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->InitNested(window_number);
	}

	const Town *GetTown() const { return Town::Get(this->window_number); }

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_MTS_CAPTION) return GetString(STR_METRO_CITY_SERVICES_CAPTION, this->window_number);
		return this->Window::GetWidgetString(widget, stringid);
	}

	/** Text of a row; @p available tells whether the service can be built now. */
	std::string GetRowText(MetroServiceType type, bool &available) const
	{
		const Town *t = this->GetTown();
		available = MetroCityIsTownActive(t) && t->cache.population >= GetMetroServiceMinPopulation(type);
		if (!available) return GetString(STR_METRO_CITY_SERVICES_ROW_LOCKED, GetMetroServiceName(type), GetMetroServiceMinPopulation(type));
		return GetString(STR_METRO_CITY_SERVICES_ROW, GetMetroServiceName(type), GetMetroServiceCost(type), CountMetroServices(t, type));
	}

	std::string GetInfoText() const
	{
		return GetString(MetroCityIsTownActive(this->GetTown()) ? STR_METRO_CITY_SERVICES_HINT : STR_METRO_CITY_SERVICES_INACTIVE);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		switch (widget) {
			case WID_MTS_LIST:
				size.height = METRO_SERVICE_COUNT * GetCharacterHeight(FontSize::Normal) + padding.height;
				break;

			case WID_MTS_INFO:
				size.height = std::max<uint>(size.height, GetStringHeight(this->GetInfoText(), size.width - padding.width) + padding.height);
				break;
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		switch (widget) {
			case WID_MTS_LIST:
				for (uint i = 0; i < METRO_SERVICE_COUNT; i++) {
					MetroServiceType type = static_cast<MetroServiceType>(i);
					bool available;
					std::string text = this->GetRowText(type, available);
					if (type == this->selected) {
						GfxFillRect(r.left + 1, ir.top, r.right - 1, ir.top + GetCharacterHeight(FontSize::Normal) - 1, PixelColour{GetColourGradient(Colours::Brown, Shade::Lighter)});
					}
					DrawString(ir, text);
					ir.top += GetCharacterHeight(FontSize::Normal);
				}
				break;

			case WID_MTS_INFO:
				DrawStringMultiLine(ir, this->GetInfoText());
				break;
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget != WID_MTS_LIST) return;
		int row = this->GetRowFromWidget(pt.y, WID_MTS_LIST, WidgetDimensions::scaled.framerect.top, GetCharacterHeight(FontSize::Normal));
		if (row < 0 || row >= static_cast<int>(METRO_SERVICE_COUNT)) return;

		MetroServiceType type = static_cast<MetroServiceType>(row);
		bool available;
		this->GetRowText(type, available);
		if (!available) return;

		if (this->selected == type) {
			ResetObjectToPlace();
			return;
		}
		/* Switching from another service aborts its placement first, which clears the selection; select afterwards. */
		SetObjectToPlaceWnd(SPR_CURSOR_HQ, PAL_NONE, HT_RECT, this);
		this->selected = type;
		this->UpdateCoverageHighlight();
		this->SetDirty();
	}

	/** Like station coverage: show the protection zone of a fire station or post around the tile under the cursor. */
	void UpdateCoverageHighlight() const
	{
		if (this->selected == MetroServiceType::End) return;
		int rad = GetMetroServiceRadius(this->selected);
		if (rad > 0) SetTileSelectBigSize(-rad, -rad, 2 * rad, 2 * rad);
	}

	void OnPaint() override
	{
		this->UpdateCoverageHighlight();
		this->DrawWidgets();
	}

	void OnPlaceObject([[maybe_unused]] Point pt, TileIndex tile) override
	{
		if (this->selected == MetroServiceType::End) return;
		if (Command<Commands::MetroBuildService>::Post(STR_METRO_CITY_ERR_CANT_BUILD, tile, static_cast<uint8_t>(this->selected)) && !_shift_pressed) {
			ResetObjectToPlace();
		}
	}

	void OnPlaceObjectAbort() override
	{
		this->selected = MetroServiceType::End;
		this->SetDirty();
	}

	void OnHundredthTick() override
	{
		this->SetDirty();
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->ReInit();
	}
};

static WindowDesc _metro_town_services_desc(__FILE__, __LINE__,
	WindowPosition::Automatic, "metro_town_services", 0, 0,
	WindowClass::MetroTownServices, WindowClass::None,
	{},
	_nested_metro_town_services_widgets
);

void ShowMetroTownServicesWindow(TownID town)
{
	AllocateWindowDescFront<MetroTownServicesWindow>(_metro_town_services_desc, town);
}

/* ---------------------------------------------------------------------------------------------- */
/* Window of a service building                                                                   */
/* ---------------------------------------------------------------------------------------------- */

/** Widgets of the #MetroServiceWindow. */
enum MetroServiceWidgets : WidgetID {
	WID_MS_CAPTION,     ///< Caption with the service and town.
	WID_MS_CENTER_VIEW, ///< Center the main view on the service.
	WID_MS_VIEWPORT,    ///< View of the service.
	WID_MS_INFO,        ///< Information about the service.
	WID_MS_REMOVE,      ///< Remove the service.
};

static constexpr std::initializer_list<NWidgetPart> _nested_metro_service_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Cream),
		NWidget(WWT_CAPTION, Colours::Cream, WID_MS_CAPTION),
		NWidget(WWT_PUSHIMGBTN, Colours::Cream, WID_MS_CENTER_VIEW), SetAspect(WidgetDimensions::ASPECT_LOCATION), SetSpriteTip(SPR_GOTO_LOCATION, STR_TOWN_VIEW_CENTER_TOOLTIP),
		NWidget(WWT_SHADEBOX, Colours::Cream),
		NWidget(WWT_STICKYBOX, Colours::Cream),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Cream),
		NWidget(WWT_INSET, Colours::Cream), SetPadding(2, 2, 2, 2),
			NWidget(NWID_VIEWPORT, Colours::Invalid, WID_MS_VIEWPORT), SetMinimalSize(254, 86), SetFill(1, 0),
		EndContainer(),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Cream, WID_MS_INFO), SetMinimalSize(260, 0), SetFill(1, 0), EndContainer(),
	NWidget(WWT_PUSHTXTBTN, Colours::Cream, WID_MS_REMOVE), SetFill(1, 0), SetStringTip(STR_METRO_SERVICE_REMOVE, STR_METRO_SERVICE_REMOVE_TOOLTIP),
};

/** Window of a school, hospital, university, fire station or fire post; opened by a click on its tile. */
struct MetroServiceWindow : Window {
	MetroServiceWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->InitNested(window_number);
		TileIndex tile{static_cast<uint32_t>(window_number)};
		NWidgetViewport *nvp = this->GetWidget<NWidgetViewport>(WID_MS_VIEWPORT);
		nvp->InitializeViewport(this, tile.base(), ScaleZoomGUI(ZoomLevel::Industry));
	}

	TileIndex GetTile() const { return TileIndex{static_cast<uint32_t>(this->window_number)}; }

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_MS_CAPTION) {
			MetroServiceType type;
			TownID town;
			int32_t build_date;
			if (MetroCityGetServiceInfo(this->GetTile(), type, town, build_date)) {
				return GetString(STR_METRO_SERVICE_CAPTION, GetMetroServiceName(type), town);
			}
		}
		return this->Window::GetWidgetString(widget, stringid);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget != WID_MS_INFO) return;
		uint height = 0;
		for (const std::string &line : MetroCityGetServiceLines(this->GetTile())) height += GetStringHeight(line, size.width - padding.width);
		size.height = std::max<uint>(size.height, height + padding.height);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_MS_INFO) return;
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		for (const std::string &line : MetroCityGetServiceLines(this->GetTile())) ir.top = DrawStringMultiLine(ir, line);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_MS_CENTER_VIEW:
				if (_ctrl_pressed) {
					ShowExtraViewportWindow(this->GetTile());
				} else {
					ScrollMainWindowToTile(this->GetTile());
				}
				break;

			case WID_MS_REMOVE:
				if (GetMetroFirePost(this->GetTile()) != nullptr) {
					Command<Commands::MetroRemoveFirePost>::Post(STR_METRO_CITY_ERR_CANT_REMOVE, this->GetTile());
				} else {
					Command<Commands::LandscapeClear>::Post(STR_ERROR_CAN_T_CLEAR_THIS_AREA, this->GetTile());
				}
				break;
		}
	}

	void OnHundredthTick() override
	{
		MetroServiceType type;
		TownID town;
		int32_t build_date;
		if (!MetroCityGetServiceInfo(this->GetTile(), type, town, build_date)) {
			this->Close();
			return;
		}
		this->SetDirty();
	}
};

static WindowDesc _metro_service_desc(__FILE__, __LINE__,
	WindowPosition::Automatic, "metro_service", 0, 0,
	WindowClass::MetroService, WindowClass::None,
	{},
	_nested_metro_service_widgets
);

void ShowMetroServiceWindow(TileIndex tile)
{
	AllocateWindowDescFront<MetroServiceWindow>(_metro_service_desc, tile.base());
}
