/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/>.
 */

/** @file metro_level_tools.h Metro mod: tools of a construction level shown inside the rail toolbar. */

#ifndef METRO_LEVEL_TOOLS_H
#define METRO_LEVEL_TOOLS_H

#include "gfx_type.h"
#include "rail_type.h"
#include "core/geometry_type.hpp"
#include "strings_type.h"
#include "tile_type.h"
#include "viewport_type.h"

#include <memory>
#include <string>

struct Window;

/**
 * Tools of one construction level (elevated +1 or underground -1..-3) drawn as a list of rows in the rail toolbar.
 * The toolbar forwards clicks on the list and the placement events of the tool to these.
 */
struct MetroLevelTools {
	int selected = -1; ///< Row of the active tool, or -1.

	virtual ~MetroLevelTools() = default;
	virtual int RowCount() const = 0;
	/** Text of a row; @p available false draws it grey. */
	virtual std::string GetRowText(int row, bool &available) const = 0;
	/** Small icon of what the row does, drawn left of its text. */
	virtual SpriteID GetRowSprite(int row) const = 0;
	virtual StringID Hint() const = 0;
	/**
	 * Click on a row: switch an option, open a window, or start placing with @p w as the placing window.
	 * @return True if a tool is now placed on the map (#selected is set).
	 */
	virtual bool Click(Window *w, int row) = 0;
	virtual void PlaceObject(TileIndex tile) = 0;
	/** End of a drag on the map. @return False if the drag is not one of these tools. */
	virtual bool PlaceMouseUp(ViewportDragDropSelectionProcess proc, TileIndex start_tile, TileIndex end_tile) = 0;
};

/* A list of tools: one row per tool, an icon left of the text (metro, elevated and power grid lists). */
uint MetroToolRowHeight();
void DrawMetroToolRow(const Rect &row, SpriteID sprite, std::string_view text, bool available, bool selected, Colours colour);

std::unique_ptr<MetroLevelTools> MetroMakeElevatedTools(RailType railtype);
std::unique_ptr<MetroLevelTools> MetroMakeUndergroundTools();

/** Levels of construction in the rail toolbar: +1 elevated, 0 ground, -1..-3 underground layers. */
static constexpr int METRO_BUILD_LEVEL_ELEVATED = 1;
static constexpr int METRO_BUILD_LEVEL_GROUND = 0;

void ShowRailToolbarAtMetroLevel(RailType railtype, int level);

#endif /* METRO_LEVEL_TOOLS_H */
