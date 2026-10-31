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
	WID_MPT_LINE,           ///< Build a power line.
	WID_MPT_LINE_REMOVE,    ///< Remove a power line.
	WID_MPT_BUILDING_FIRST, ///< First building (substation); the others follow.
	WID_MPT_BUILDING_LAST = WID_MPT_BUILDING_FIRST + METRO_POWER_BUILDING_COUNT - 1,
	WID_MPT_INFO,           ///< The chosen tool: price, power, year; and the hint.
};

/** Icon of a tool button (vanilla sprites for now; see DESIGN.md "Спрайты"). */
static std::unique_ptr<NWidgetBase> MakeMetroPowerButtons()
{
	auto hor = std::make_unique<NWidgetHorizontal>();
	auto add = [&](WidgetID widget, SpriteID sprite, StringID tip) {
		auto leaf = std::make_unique<NWidgetLeaf>(WWT_IMGBTN, Colours::DarkGreen, widget, WidgetData{.sprite = sprite}, tip);
		leaf->SetMinimalSize(22, 22);
		leaf->SetFill(0, 1);
		hor->Add(std::move(leaf));
	};
	add(WID_MPT_LINE, METRO_POWER_ICON_LINE, STR_METRO_POWER_TOOLTIP_LINE);
	add(WID_MPT_LINE_REMOVE, METRO_POWER_ICON_LINE_REMOVE, STR_METRO_POWER_TOOLTIP_LINE_REMOVE);
	for (uint i = 0; i < METRO_POWER_BUILDING_COUNT; i++) {
		add(WID_MPT_BUILDING_FIRST + i, _metro_power_building_icons[i], STR_METRO_POWER_TOOLTIP_BUILDING);
	}
	auto filler = std::make_unique<NWidgetBackground>(WWT_PANEL, Colours::DarkGreen, -1);
	filler->SetFill(1, 1);
	filler->SetResize(1, 0);
	hor->Add(std::move(filler));
	return hor;
}

static constexpr std::initializer_list<NWidgetPart> _nested_metro_power_toolbar_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::DarkGreen),
		NWidget(WWT_CAPTION, Colours::DarkGreen), SetStringTip(STR_METRO_POWER_TOOLBAR_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::DarkGreen),
		NWidget(WWT_STICKYBOX, Colours::DarkGreen),
	EndContainer(),
	NWidgetFunction(MakeMetroPowerButtons),
	NWidget(WWT_PANEL, Colours::DarkGreen, WID_MPT_INFO), SetMinimalSize(340, 0), SetFill(1, 1), SetResize(1, 0), EndContainer(),
};

/** Construction of the grid: click a tool button, then the map. */
struct MetroPowerToolbarWindow : Window {
	int selected = -1; ///< Tool being placed: widget of the button.

	MetroPowerToolbarWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->InitNested(window_number);
		this->UpdateDisabled();
	}

	/** Text of a tool; @p available tells whether it can be used now. */
	std::string GetToolText(int widget, bool &available) const
	{
		available = true;
		if (widget == WID_MPT_LINE) return GetString(STR_METRO_POWER_ROW_LINE, GetMetroPowerLineCost());
		if (widget == WID_MPT_LINE_REMOVE) return GetString(STR_METRO_POWER_ROW_LINE_REMOVE);
		MetroPowerBuilding building = static_cast<MetroPowerBuilding>(widget - WID_MPT_BUILDING_FIRST);
		available = IsMetroPowerBuildingAvailable(building);
		if (!available) return GetString(STR_METRO_POWER_ROW_LOCKED, GetMetroPowerBuildingName(building), GetMetroPowerBuildingIntroYear(building));
		uint32_t kw = GetMetroPowerBuildingPowerKw(building);
		if (building == MetroPowerBuilding::Substation) return GetString(STR_METRO_POWER_ROW_SUBSTATION, GetMetroPowerBuildingName(building), GetMetroPowerBuildingCost(building), METRO_POWER_SUBSTATION_RADIUS);
		if (building == MetroPowerBuilding::VentShaft) return GetString(STR_METRO_POWER_ROW_CONSUMER, GetMetroPowerBuildingName(building), GetMetroPowerBuildingCost(building), kw);
		return GetString(STR_METRO_POWER_ROW_PLANT, GetMetroPowerBuildingName(building), GetMetroPowerBuildingCost(building), kw);
	}

	void UpdateDisabled()
	{
		for (uint i = 0; i < METRO_POWER_BUILDING_COUNT; i++) {
			this->SetWidgetDisabledState(WID_MPT_BUILDING_FIRST + i, !IsMetroPowerBuildingAvailable(static_cast<MetroPowerBuilding>(i)));
		}
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget != WID_MPT_INFO) return;
		/* All tools listed, then the hint. */
		uint lines = 2 + METRO_POWER_BUILDING_COUNT;
		size.height = std::max<uint>(size.height, lines * GetCharacterHeight(FontSize::Normal) + GetStringHeight(STR_METRO_POWER_TOOLBAR_HINT, size.width - padding.width) + padding.height + WidgetDimensions::scaled.vsep_wide);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_MPT_INFO) return;
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		for (int w = WID_MPT_LINE; w <= WID_MPT_BUILDING_LAST; w++) {
			bool available;
			std::string text = this->GetToolText(w, available);
			if (w == this->selected) {
				GfxFillRect(r.left + 1, ir.top, r.right - 1, ir.top + GetCharacterHeight(FontSize::Normal) - 1, PixelColour{GetColourGradient(Colours::DarkGreen, Shade::Lighter)});
			}
			DrawString(ir, text, available ? TextColour::Black : TextColour::Grey);
			ir.top += GetCharacterHeight(FontSize::Normal);
		}
		ir.top += WidgetDimensions::scaled.vsep_wide;
		DrawStringMultiLine(ir, STR_METRO_POWER_TOOLBAR_HINT);
	}

	bool OnTooltip([[maybe_unused]] Point pt, WidgetID widget, TooltipCloseCondition close_cond) override
	{
		if (widget < WID_MPT_LINE || widget > WID_MPT_BUILDING_LAST) return false;
		bool available;
		GuiShowTooltips(this, GetEncodedRawString(this->GetToolText(widget, available)), close_cond);
		return true;
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget < WID_MPT_LINE || widget > WID_MPT_BUILDING_LAST) return;
		bool available;
		this->GetToolText(widget, available);
		if (!available) return;

		if (this->selected == widget) {
			ResetObjectToPlace();
			return;
		}
		/* Switching from another tool aborts its placement first, which clears the selection; select afterwards. */
		CursorID cursor = widget == WID_MPT_LINE_REMOVE ? SPR_CURSOR_DEMOLISH_FIRST : (widget == WID_MPT_LINE ? SPR_CURSOR_NS_TRACK : SPR_CURSOR_TRANSMITTER);
		SetObjectToPlaceWnd(cursor, PAL_NONE, HT_RECT, this);
		this->selected = widget;
		this->LowerWidget(widget);
		this->UpdateSelectSize();
		this->SetDirty();
	}

	/** Size of the building under the cursor and the feeding radius of a substation or vent shaft. */
	void UpdateSelectSize()
	{
		if (this->selected < WID_MPT_BUILDING_FIRST) {
			SetTileSelectSize(1, 1);
			return;
		}
		MetroPowerBuilding building = static_cast<MetroPowerBuilding>(this->selected - WID_MPT_BUILDING_FIRST);
		const ObjectSpec *spec = ObjectSpec::Get(_metro_power_building_specs[to_underlying(building)].object);
		SetTileSelectSize(GB(spec->size, 0, 4), GB(spec->size, 4, 4));
		if (building == MetroPowerBuilding::Substation) {
			int rad = METRO_POWER_SUBSTATION_RADIUS;
			SetTileSelectBigSize(-rad, -rad, 2 * rad, 2 * rad);
		}
	}

	void OnMouseLoop() override
	{
		if (this->selected >= WID_MPT_BUILDING_FIRST) this->UpdateSelectSize();
	}

	void OnPlaceObject([[maybe_unused]] Point pt, TileIndex tile) override
	{
		if (this->selected == WID_MPT_LINE) {
			VpStartPlaceSizing(tile, VPM_X_OR_Y, DDSP_METRO_POWER_LINE);
			return;
		}
		if (this->selected == WID_MPT_LINE_REMOVE) {
			VpStartPlaceSizing(tile, VPM_X_OR_Y, DDSP_METRO_POWER_LINE_REMOVE);
			return;
		}
		if (this->selected < WID_MPT_BUILDING_FIRST) return;
		uint8_t building = static_cast<uint8_t>(this->selected - WID_MPT_BUILDING_FIRST);
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
		if (this->selected >= 0) this->RaiseWidget(this->selected);
		this->selected = -1;
		this->SetDirty();
	}

	void OnHundredthTick() override
	{
		this->UpdateDisabled();
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
