/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_power_gui.cpp Metro mod: power grid construction window and the window of a grid building with its network. */

#include "stdafx.h"
#include "metro_power.h"
#include "metro_power_cmd.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "gfx_func.h"
#include "gui.h"
#include "landscape_cmd.h"
#include "object_base.h"
#include "newgrf_object.h"
#include "strings_func.h"
#include "tilehighlight_func.h"
#include "viewport_func.h"
#include "window_func.h"
#include "window_gui.h"
#include "zoom_func.h"
#include "table/metro_power_tables.h"
#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

/* ---------------------------------------------------------------------------------------------- */
/* Construction window                                                                            */
/* ---------------------------------------------------------------------------------------------- */

/** Widgets of the #MetroPowerToolbarWindow. */
enum MetroPowerToolbarWidgets : WidgetID {
	WID_MPT_LIST, ///< Tools: power line, its removal and the buildings.
	WID_MPT_INFO, ///< Hint.
};

static constexpr std::initializer_list<NWidgetPart> _nested_metro_power_toolbar_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::DarkGreen),
		NWidget(WWT_CAPTION, Colours::DarkGreen), SetStringTip(STR_METRO_POWER_TOOLBAR_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::DarkGreen),
		NWidget(WWT_STICKYBOX, Colours::DarkGreen),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::DarkGreen, WID_MPT_LIST), SetMinimalSize(340, 0), SetFill(1, 0), SetResize(1, 0), SetToolTip(STR_METRO_POWER_TOOLBAR_LIST_TOOLTIP), EndContainer(),
	NWidget(WWT_PANEL, Colours::DarkGreen, WID_MPT_INFO), SetMinimalSize(340, 0), SetFill(1, 1), SetResize(1, 0), EndContainer(),
};

/** Rows of the construction window: the two line tools, then the buildings. */
static constexpr int METRO_POWER_ROW_LINE = 0;
static constexpr int METRO_POWER_ROW_LINE_REMOVE = 1;
static constexpr int METRO_POWER_ROW_FIRST_BUILDING = 2;
static constexpr int METRO_POWER_ROW_COUNT = METRO_POWER_ROW_FIRST_BUILDING + METRO_POWER_BUILDING_COUNT;

/** Construction of the grid: click a row, then the map. */
struct MetroPowerToolbarWindow : Window {
	int selected = -1; ///< Row being placed.

	MetroPowerToolbarWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->InitNested(window_number);
	}

	/** Text of a row; @p available tells whether it can be used now. */
	std::string GetRowText(int row, bool &available) const
	{
		available = true;
		if (row == METRO_POWER_ROW_LINE) return GetString(STR_METRO_POWER_ROW_LINE, GetMetroPowerLineCost());
		if (row == METRO_POWER_ROW_LINE_REMOVE) return GetString(STR_METRO_POWER_ROW_LINE_REMOVE);
		MetroPowerBuilding building = static_cast<MetroPowerBuilding>(row - METRO_POWER_ROW_FIRST_BUILDING);
		available = IsMetroPowerBuildingAvailable(building);
		if (!available) return GetString(STR_METRO_POWER_ROW_LOCKED, GetMetroPowerBuildingName(building), GetMetroPowerBuildingIntroYear(building));
		uint32_t kw = GetMetroPowerBuildingPowerKw(building);
		if (building == MetroPowerBuilding::Substation) return GetString(STR_METRO_POWER_ROW_SUBSTATION, GetMetroPowerBuildingName(building), GetMetroPowerBuildingCost(building), METRO_POWER_SUBSTATION_RADIUS);
		if (building == MetroPowerBuilding::VentShaft) return GetString(STR_METRO_POWER_ROW_CONSUMER, GetMetroPowerBuildingName(building), GetMetroPowerBuildingCost(building), kw);
		return GetString(STR_METRO_POWER_ROW_PLANT, GetMetroPowerBuildingName(building), GetMetroPowerBuildingCost(building), kw);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		switch (widget) {
			case WID_MPT_LIST:
				size.height = METRO_POWER_ROW_COUNT * GetCharacterHeight(FontSize::Normal) + padding.height;
				break;

			case WID_MPT_INFO:
				size.height = std::max<uint>(size.height, GetStringHeight(STR_METRO_POWER_TOOLBAR_HINT, size.width - padding.width) + padding.height);
				break;
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		switch (widget) {
			case WID_MPT_LIST:
				for (int row = 0; row < METRO_POWER_ROW_COUNT; row++) {
					bool available;
					std::string text = this->GetRowText(row, available);
					if (row == this->selected) {
						GfxFillRect(r.left + 1, ir.top, r.right - 1, ir.top + GetCharacterHeight(FontSize::Normal) - 1, PixelColour{GetColourGradient(Colours::DarkGreen, Shade::Lighter)});
					}
					DrawString(ir, text, available ? TextColour::Black : TextColour::Grey);
					ir.top += GetCharacterHeight(FontSize::Normal);
				}
				break;

			case WID_MPT_INFO:
				DrawStringMultiLine(ir, STR_METRO_POWER_TOOLBAR_HINT);
				break;
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget != WID_MPT_LIST) return;
		int row = this->GetRowFromWidget(pt.y, WID_MPT_LIST, WidgetDimensions::scaled.framerect.top, GetCharacterHeight(FontSize::Normal));
		if (row < 0 || row >= METRO_POWER_ROW_COUNT) return;
		bool available;
		this->GetRowText(row, available);
		if (!available) return;

		if (this->selected == row) {
			ResetObjectToPlace();
			return;
		}
		/* Switching from another tool aborts its placement first, which clears the selection; select afterwards. */
		CursorID cursor = row == METRO_POWER_ROW_LINE_REMOVE ? SPR_CURSOR_DEMOLISH_FIRST : (row == METRO_POWER_ROW_LINE ? SPR_CURSOR_NS_TRACK : SPR_CURSOR_TRANSMITTER);
		SetObjectToPlaceWnd(cursor, PAL_NONE, HT_RECT, this);
		this->selected = row;
		this->UpdateSelectSize();
		this->SetDirty();
	}

	/** Size of the building under the cursor and the feeding radius of a substation or vent shaft. */
	void UpdateSelectSize()
	{
		if (this->selected < METRO_POWER_ROW_FIRST_BUILDING) {
			SetTileSelectSize(1, 1);
			return;
		}
		MetroPowerBuilding building = static_cast<MetroPowerBuilding>(this->selected - METRO_POWER_ROW_FIRST_BUILDING);
		const ObjectSpec *spec = ObjectSpec::Get(_metro_power_building_specs[to_underlying(building)].object);
		SetTileSelectSize(GB(spec->size, 0, 4), GB(spec->size, 4, 4));
		if (building == MetroPowerBuilding::Substation) {
			int rad = METRO_POWER_SUBSTATION_RADIUS;
			SetTileSelectBigSize(-rad, -rad, 2 * rad, 2 * rad);
		}
	}

	void OnMouseLoop() override
	{
		if (this->selected >= METRO_POWER_ROW_FIRST_BUILDING) this->UpdateSelectSize();
	}

	void OnPlaceObject([[maybe_unused]] Point pt, TileIndex tile) override
	{
		if (this->selected == METRO_POWER_ROW_LINE) {
			VpStartPlaceSizing(tile, VPM_X_OR_Y, DDSP_METRO_POWER_LINE);
			return;
		}
		if (this->selected == METRO_POWER_ROW_LINE_REMOVE) {
			VpStartPlaceSizing(tile, VPM_X_OR_Y, DDSP_METRO_POWER_LINE_REMOVE);
			return;
		}
		if (this->selected < 0) return;
		uint8_t building = static_cast<uint8_t>(this->selected - METRO_POWER_ROW_FIRST_BUILDING);
		if (Command<Commands::MetroBuildPowerObject>::Post(STR_METRO_POWER_ERR_CANT_BUILD, tile, building) && !_shift_pressed) {
			ResetObjectToPlace();
		}
	}

	void OnPlaceDrag(ViewportPlaceMethod select_method, [[maybe_unused]] ViewportDragDropSelectionProcess select_proc, Point pt) override
	{
		VpSelectTilesWithMethod(pt.x, pt.y, select_method);
	}

	void OnPlaceMouseUp([[maybe_unused]] ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, [[maybe_unused]] Point pt, TileIndex start_tile, TileIndex end_tile) override
	{
		if (pt.x == -1) return;
		bool remove = select_proc == DDSP_METRO_POWER_LINE_REMOVE;
		if (select_proc != DDSP_METRO_POWER_LINE && !remove) return;
		Command<Commands::MetroBuildPowerLine>::Post(remove ? STR_METRO_POWER_ERR_CANT_REMOVE_LINE : STR_METRO_POWER_ERR_CANT_BUILD_LINE, end_tile, start_tile, remove);
	}

	void OnPlaceObjectAbort() override
	{
		this->selected = -1;
		this->SetDirty();
	}

	void OnHundredthTick() override
	{
		this->SetDirty();
	}
};

static WindowDesc _metro_power_toolbar_desc(__FILE__, __LINE__,
	WindowPosition::Automatic, "metro_power_toolbar", 0, 0,
	WindowClass::MetroPowerToolbar, WindowClass::None,
	WindowDefaultFlag::Construction,
	_nested_metro_power_toolbar_widgets
);

void ShowMetroPowerToolbar()
{
	if (!Company::IsValidID(_local_company)) return;
	AllocateWindowDescFront<MetroPowerToolbarWindow>(_metro_power_toolbar_desc, 0);
}

/* ---------------------------------------------------------------------------------------------- */
/* Window of a grid building and its network                                                      */
/* ---------------------------------------------------------------------------------------------- */

/** Widgets of the #MetroPowerNetworkWindow. */
enum MetroPowerNetworkWidgets : WidgetID {
	WID_MPN_CAPTION,     ///< Caption with the building.
	WID_MPN_CENTER_VIEW, ///< Center the main view on the building.
	WID_MPN_VIEWPORT,    ///< View of the building.
	WID_MPN_INFO,        ///< Building and network.
	WID_MPN_PRIORITY,    ///< Swap the blackout priority.
	WID_MPN_REMOVE,      ///< Remove the building.
};

static constexpr std::initializer_list<NWidgetPart> _nested_metro_power_network_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::DarkGreen),
		NWidget(WWT_CAPTION, Colours::DarkGreen, WID_MPN_CAPTION),
		NWidget(WWT_PUSHIMGBTN, Colours::DarkGreen, WID_MPN_CENTER_VIEW), SetAspect(WidgetDimensions::ASPECT_LOCATION), SetSpriteTip(SPR_GOTO_LOCATION, STR_TOWN_VIEW_CENTER_TOOLTIP),
		NWidget(WWT_SHADEBOX, Colours::DarkGreen),
		NWidget(WWT_STICKYBOX, Colours::DarkGreen),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::DarkGreen),
		NWidget(WWT_INSET, Colours::DarkGreen), SetPadding(2, 2, 2, 2),
			NWidget(NWID_VIEWPORT, Colours::Invalid, WID_MPN_VIEWPORT), SetMinimalSize(314, 86), SetFill(1, 0),
		EndContainer(),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::DarkGreen, WID_MPN_INFO), SetMinimalSize(320, 0), SetFill(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_PUSHTXTBTN, Colours::DarkGreen, WID_MPN_PRIORITY), SetFill(1, 0), SetStringTip(STR_METRO_POWER_PRIORITY_BUTTON, STR_METRO_POWER_PRIORITY_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::DarkGreen, WID_MPN_REMOVE), SetFill(1, 0), SetStringTip(STR_METRO_POWER_REMOVE, STR_METRO_POWER_REMOVE_TOOLTIP),
	EndContainer(),
};

/** Window of a plant, substation or vent shaft: the building and its network; opened by a click on its tile. */
struct MetroPowerNetworkWindow : Window {
	MetroPowerNetworkWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->InitNested(window_number);
		NWidgetViewport *nvp = this->GetWidget<NWidgetViewport>(WID_MPN_VIEWPORT);
		nvp->InitializeViewport(this, this->GetTile().base(), ScaleZoomGUI(ZoomLevel::Industry));
	}

	TileIndex GetTile() const { return TileIndex{static_cast<uint32_t>(this->window_number)}; }

	const Object *GetObject() const
	{
		TileIndex tile = this->GetTile();
		if (!IsTileType(tile, TileType::Object)) return nullptr;
		const Object *o = Object::GetByTile(tile);
		return IsMetroPowerObject(o->type) ? o : nullptr;
	}

	std::vector<std::string> GetLines() const
	{
		std::vector<std::string> lines = MetroPowerGetBuildingLines(this->GetTile());
		MetroPowerNetworkInfo info = MetroPowerGetNetworkInfo(this->GetTile());
		if (!info.valid) return lines;
		lines.push_back(GetString(STR_METRO_POWER_NET_HEADER, info.line_tiles));
		lines.push_back(GetString(STR_METRO_POWER_NET_CAPACITY, info.capacity_kw, info.nominal_kw));
		lines.push_back(GetString(STR_METRO_POWER_NET_DEMAND, info.vent_demand_kw + info.track_demand_kw, info.vent_demand_kw, info.track_demand_kw, info.supplied_kw));
		for (const std::string &plant : info.plants) lines.push_back(plant);
		lines.push_back(GetString(STR_METRO_POWER_NET_SUBSTATIONS, info.substations, info.substations_off, info.trains, info.trains_unpowered));
		lines.push_back(GetString(STR_METRO_POWER_NET_VENTS, info.vents, info.vents_diesel, info.vents_off));
		lines.push_back(GetString(info.tracks_first ? STR_METRO_POWER_NET_PRIORITY_TRACKS : STR_METRO_POWER_NET_PRIORITY_VENTS));
		if (info.vent_demand_kw + info.track_demand_kw > info.supplied_kw) lines.push_back(GetString(STR_METRO_POWER_NET_BLACKOUT));
		return lines;
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_MPN_CAPTION) {
			if (const Object *o = this->GetObject(); o != nullptr) return GetString(STR_METRO_POWER_CAPTION, GetMetroPowerBuildingName(GetMetroPowerBuildingOfObject(o->type)));
		}
		return this->Window::GetWidgetString(widget, stringid);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget != WID_MPN_INFO) return;
		uint height = 0;
		for (const std::string &line : this->GetLines()) height += GetStringHeight(line, size.width - padding.width);
		/* Room for the network growing while the window is open. */
		size.height = std::max<uint>(size.height, height + 4 * GetCharacterHeight(FontSize::Normal) + padding.height);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_MPN_INFO) return;
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		for (const std::string &line : this->GetLines()) ir.top = DrawStringMultiLine(ir, line);
	}

	void OnPaint() override
	{
		const Object *o = this->GetObject();
		bool own = o != nullptr && GetTileOwner(this->GetTile()) == _local_company;
		this->SetWidgetDisabledState(WID_MPN_PRIORITY, !own);
		this->SetWidgetDisabledState(WID_MPN_REMOVE, !own);
		this->DrawWidgets();
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_MPN_CENTER_VIEW:
				if (_ctrl_pressed) {
					ShowExtraViewportWindow(this->GetTile());
				} else {
					ScrollMainWindowToTile(this->GetTile());
				}
				break;

			case WID_MPN_PRIORITY: {
				MetroPowerNetworkInfo info = MetroPowerGetNetworkInfo(this->GetTile());
				Command<Commands::MetroSetPowerPriority>::Post(!info.tracks_first);
				break;
			}

			case WID_MPN_REMOVE:
				Command<Commands::LandscapeClear>::Post(STR_ERROR_CAN_T_CLEAR_THIS_AREA, this->GetTile());
				break;
		}
	}

	void OnHundredthTick() override
	{
		if (this->GetObject() == nullptr) {
			this->Close();
			return;
		}
		this->SetDirty();
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->SetDirty();
	}
};

static WindowDesc _metro_power_network_desc(__FILE__, __LINE__,
	WindowPosition::Automatic, "metro_power_network", 0, 0,
	WindowClass::MetroPowerNetwork, WindowClass::None,
	{},
	_nested_metro_power_network_widgets
);

void ShowMetroPowerNetworkWindow(TileIndex tile)
{
	/* Several tiles of a plant open the same window: use its north tile. */
	if (IsTileType(tile, TileType::Object)) tile = Object::GetByTile(tile)->location.tile;
	AllocateWindowDescFront<MetroPowerNetworkWindow>(_metro_power_network_desc, tile.base());
}
