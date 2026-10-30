/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_town_gui.cpp Metro mod: district map of a town (the districts are coloured only here). */

#include "stdafx.h"
#include "metro_town.h"
#include "gfx_func.h"
#include "road_map.h"
#include "strings_func.h"
#include "tile_map.h"
#include "town.h"
#include "town_map.h"
#include "window_func.h"
#include "window_gui.h"
#include "zoom_func.h"
#include "table/metro_town_tables.h"
#include "table/strings.h"

#include "safeguards.h"

/** Widgets of the #MetroDistrictsWindow. */
enum MetroDistrictsWidgets : WidgetID {
	WID_MD_CAPTION, ///< Caption with the town name.
	WID_MD_MAP,     ///< District map.
	WID_MD_INFO,    ///< Legend and numbers.
};

static constexpr std::initializer_list<NWidgetPart> _nested_metro_districts_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Brown),
		NWidget(WWT_CAPTION, Colours::Brown, WID_MD_CAPTION),
		NWidget(WWT_SHADEBOX, Colours::Brown),
		NWidget(WWT_STICKYBOX, Colours::Brown),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Brown),
		NWidget(WWT_INSET, Colours::Brown, WID_MD_MAP), SetPadding(2, 2, 2, 2), SetMinimalSize(200, 200), SetFill(1, 1), EndContainer(),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Brown, WID_MD_INFO), SetMinimalSize(220, 0), SetFill(1, 0), EndContainer(),
};

/** Map of the districts of a town: houses coloured by district, roads grey, district centres white. */
struct MetroDistrictsWindow : Window {
	MetroDistrictsWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->InitNested(window_number);
	}

	const Town *GetTown() const { return Town::GetIfValid(this->window_number); }

	std::vector<std::string> GetLines() const
	{
		std::vector<std::string> lines;
		const Town *t = this->GetTown();
		if (t == nullptr) return lines;
		std::vector<MetroDistrictStats> stats = MetroTownGetDistrictStats(t);
		if (stats[0].districts + stats[1].districts == 0) {
			lines.push_back(GetString(STR_METRO_TOWN_DISTRICTS_NONE));
			return lines;
		}
		for (uint i = 0; i < stats.size(); i++) {
			lines.push_back(GetString(STR_METRO_TOWN_DISTRICTS_LEGEND, _metro_district_specs[i].name, stats[i].districts, stats[i].houses, stats[i].population));
		}
		lines.push_back(GetString(STR_METRO_TOWN_JOBS_LINE, MetroTownGetWorkers(t), MetroTownGetJobs(t)));
		return lines;
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_MD_CAPTION) return GetString(STR_METRO_TOWN_DISTRICTS_CAPTION, this->window_number);
		return this->Window::GetWidgetString(widget, stringid);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget == WID_MD_MAP) {
			size.width = std::max<uint>(size.width, ScaleGUITrad(200));
			size.height = std::max<uint>(size.height, ScaleGUITrad(200));
			return;
		}
		if (widget != WID_MD_INFO) return;
		uint height = 0;
		for (const std::string &line : this->GetLines()) height += GetStringHeight(line, size.width - padding.width);
		size.height = std::max<uint>(size.height, height + padding.height);
	}

	void DrawMap(const Rect &r) const
	{
		const Town *t = this->GetTown();
		GfxFillRect(r, PC_BLACK);
		if (t == nullptr) return;
		int radius = MetroTownGetMapRadius(t);
		int cells = 2 * radius + 1;
		int cell = std::max(1, std::min(r.Width(), r.Height()) / cells);
		int left = r.left + (r.Width() - cell * cells) / 2;
		int top = r.top + (r.Height() - cell * cells) / 2;
		int cx = TileX(t->xy);
		int cy = TileY(t->xy);
		for (int dy = -radius; dy <= radius; dy++) {
			for (int dx = -radius; dx <= radius; dx++) {
				int x = cx + dx;
				int y = cy + dy;
				if (x < 0 || y < 0 || x >= static_cast<int>(Map::SizeX()) || y >= static_cast<int>(Map::SizeY())) continue;
				TileIndex tile = TileXY(x, y);
				PixelColour colour = PC_DARK_GREY;
				if (IsTileType(tile, TileType::House) && GetTownIndex(tile) == t->index) {
					int type = GetMetroDistrictTypeAt(t, tile);
					colour = type < 0 ? PC_WHITE : _metro_district_specs[type].colour;
				} else if (IsTileType(tile, TileType::Road)) {
					colour = PC_GREY;
				} else if (IsTileType(tile, TileType::Water)) {
					colour = PC_WATER;
				}
				int px = left + (dx + radius) * cell;
				int py = top + (dy + radius) * cell;
				GfxFillRect(px, py, px + cell - 1, py + cell - 1, colour);
			}
		}
		/* District centres. */
		for (const MetroDistrict &d : _metro_districts) {
			if (d.town != t->index.base()) continue;
			int dx = static_cast<int>(TileX(TileIndex{d.tile})) - cx;
			int dy = static_cast<int>(TileY(TileIndex{d.tile})) - cy;
			if (std::abs(dx) > radius || std::abs(dy) > radius) continue;
			int px = left + (dx + radius) * cell;
			int py = top + (dy + radius) * cell;
			GfxFillRect(px, py, px + cell - 1, py + cell - 1, PC_WHITE);
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		switch (widget) {
			case WID_MD_MAP:
				this->DrawMap(r.Shrink(WidgetDimensions::scaled.bevel));
				break;

			case WID_MD_INFO: {
				Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
				for (const std::string &line : this->GetLines()) ir.top = DrawStringMultiLine(ir, line);
				break;
			}
		}
	}

	void OnHundredthTick() override
	{
		if (this->GetTown() == nullptr) {
			this->Close();
			return;
		}
		this->SetDirty();
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->ReInit();
	}
};

static WindowDesc _metro_districts_desc(__FILE__, __LINE__,
	WindowPosition::Automatic, "metro_town_districts", 0, 0,
	WindowClass::MetroTownDistricts, WindowClass::None,
	{},
	_nested_metro_districts_widgets
);

void ShowMetroDistrictsWindow(TownID town)
{
	AllocateWindowDescFront<MetroDistrictsWindow>(_metro_districts_desc, town);
}
