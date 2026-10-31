/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_underground.cpp Metro mod: underground layers -1..-3, their track and the entrances (stage 8, step 1).
 *
 * Layer -1 opens in 1960 and lies only under open land, roads and small houses; layer -2 opens halfway between the
 * electrified rail and the monorail (at least 5 years after -1) and lies under any building but not under water;
 * layer -3 opens halfway between the monorail and the maglev and lies everywhere, rivers included. The years of the
 * rail types are those of their first engines in this climate. Trains, inclines, stations and depots come in the
 * next steps; for now the track is built, removed, saved and shown on a chosen layer.
 */

#include "stdafx.h"
#include "metro_underground.h"
#include "metro_underground_cmd.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "console_func.h"
#include "console_type.h"
#include "date_func.h"
#include "economy_func.h"
#include "engine_base.h"
#include "gfx_func.h"
#include "gui.h"
#include "house.h"
#include "landscape.h"
#include "landscape_cmd.h"
#include "object.h"
#include "object_base.h"
#include "object_map.h"
#include "rail.h"
#include "road_map.h"
#include "slope_func.h"
#include "sprite.h"
#include "strings_func.h"
#include "tile_cmd.h"
#include "tilehighlight_func.h"
#include "town.h"
#include "town_map.h"
#include "viewport_func.h"
#include "water_map.h"
#include "window_func.h"
#include "window_gui.h"
#include "core/backup_type.hpp"
#include "core/string_consumer.hpp"
#include "table/strings.h"
#include "table/sprites.h"
#include "table/metro_underground_tables.h"

#include "safeguards.h"

std::map<uint64_t, MetroUndergroundTile> _metro_underground;
uint8_t _metro_underground_view_layer = 0;


void MetroUndergroundReset()
{
	_metro_underground.clear();
	_metro_underground_view_layer = 0;
}

const MetroUndergroundTile *GetMetroUndergroundTile(TileIndex tile, uint layer)
{
	auto it = _metro_underground.find(MetroUndergroundKey(tile, layer));
	return it == _metro_underground.end() ? nullptr : &it->second;
}

bool IsMetroEntranceObject(ObjectType type)
{
	return type == OBJECT_METRO_ENTRANCE;
}

/* ---------------------------------------------------------------------------------------------- */
/* Layers                                                                                         */
/* ---------------------------------------------------------------------------------------------- */

/** Year of the first engine (not wagon) for the rail type in this climate, or @p fallback. */
static int32_t GetRailTypeFirstYear(RailTypeLabel label, int32_t fallback)
{
	RailType rt = GetRailTypeByLabel(label, false);
	if (rt == INVALID_RAILTYPE) return fallback;
	CalTime::Date first = CalTime::MAX_DATE;
	for (const Engine *e : Engine::IterateType(VehicleType::Train)) {
		if (!e->info.climates.Test(_settings_game.game_creation.landscape)) continue;
		const RailVehicleInfo &rvi = e->VehInfo<RailVehicleInfo>();
		if (!rvi.railtypes.Test(rt) || rvi.railveh_type == RailVehicleType::Wagon || rvi.power == 0) continue; // Engines only: wagons come long before.
		first = std::min(first, e->info.base_intro);
	}
	if (first == CalTime::MAX_DATE) return fallback;
	return CalTime::ConvertDateToYMD(first).year.base();
}

/** Year the layer (1..3) opens. */
int32_t GetMetroLayerYear(uint layer)
{
	int32_t electric = GetRailTypeFirstYear(RAILTYPE_LABEL_ELECTRIC, METRO_FALLBACK_ELECTRIC_YEAR);
	int32_t monorail = GetRailTypeFirstYear(RAILTYPE_LABEL_MONO, METRO_FALLBACK_MONORAIL_YEAR);
	int32_t maglev = GetRailTypeFirstYear(RAILTYPE_LABEL_MAGLEV, METRO_FALLBACK_MAGLEV_YEAR);
	switch (layer) {
		case 1: return METRO_LAYER_1_YEAR;
		case 2: return std::max((electric + monorail) / 2, METRO_LAYER_1_YEAR + METRO_LAYER_2_MIN_GAP);
		case 3: return std::max((monorail + maglev) / 2, METRO_LAYER_1_YEAR + METRO_LAYER_2_MIN_GAP);
		default: return INT32_MAX;
	}
}

bool IsMetroLayerAvailable(uint layer)
{
	return layer >= 1 && layer <= METRO_LAYER_COUNT && CalTime::CurYear().base() >= GetMetroLayerYear(layer);
}

/** A small house: 1x1 and few residents. */
static bool IsSmallHouse(TileIndex tile)
{
	const HouseSpec *hs = HouseSpec::Get(GetHouseType(tile));
	return hs->building_flags.Test(BuildingFlag::Size1x1) && hs->population <= METRO_SMALL_HOUSE_POPULATION;
}

/** Whether the layer may lie under the surface tile. */
CommandCost MetroCheckUndergroundSurface(TileIndex tile, uint layer)
{
	if (TileX(tile) == 0 || TileY(tile) == 0 || TileX(tile) >= Map::MaxX() || TileY(tile) >= Map::MaxY()) return CommandCost(STR_ERROR_TOO_CLOSE_TO_EDGE_OF_MAP);
	switch (layer) {
		case 1:
			switch (GetTileType(tile)) {
				case TileType::Clear:
				case TileType::Trees:
					return CommandCost();
				case TileType::Road:
					if (IsNormalRoadTile(tile)) return CommandCost();
					break;
				case TileType::House:
					if (IsSmallHouse(tile)) return CommandCost();
					break;
				default:
					break;
			}
			return CommandCost(STR_METRO_UNDERGROUND_ERR_LAYER_1_SURFACE);

		case 2:
			if (IsTileType(tile, TileType::Water) || (HasTileWaterClass(tile) && IsTileOnWater(tile))) return CommandCost(STR_METRO_UNDERGROUND_ERR_LAYER_2_WATER);
			return CommandCost();

		case 3:
			return CommandCost();

		default:
			return CMD_ERROR;
	}
}

/* ---------------------------------------------------------------------------------------------- */
/* Commands                                                                                       */
/* ---------------------------------------------------------------------------------------------- */

static Money GetUndergroundPiecePrice(uint layer)
{
	return _price[Price::BuildRail] * _metro_layer_specs[layer - 1].cost_percent / 100;
}

/**
 * Build or remove underground track on a layer: one piece on one tile, or a straight line along X or Y.
 * @param flags type of operation
 * @param tile end tile
 * @param start start tile
 * @param layer 1..3 for -1..-3
 * @param track the piece (for a line: TRACK_X or TRACK_Y along it)
 * @param remove remove instead of build
 */
CommandCost CmdMetroBuildUndergroundTrack(DoCommandFlags flags, TileIndex tile, TileIndex start, uint8_t layer, Track track, bool remove)
{
	if (start >= Map::Size() || layer < 1 || layer > METRO_LAYER_COUNT || !IsValidTrack(track) || !Company::IsValidID(_current_company)) return CMD_ERROR;
	if (!remove && !IsMetroLayerAvailable(layer)) return CommandCostWithParam(STR_METRO_UNDERGROUND_ERR_LAYER_CLOSED, GetMetroLayerYear(layer));
	if (start != tile) {
		if (track != TRACK_X && track != TRACK_Y) return CMD_ERROR;
		if ((track == TRACK_X && TileY(start) != TileY(tile)) || (track == TRACK_Y && TileX(start) != TileX(tile))) return CommandCost(STR_METRO_UNDERGROUND_ERR_STRAIGHT);
	}

	TrackBits bit = TrackToTrackBits(track);
	CommandCost cost(ExpensesType::Construction);
	CommandCost last_error(remove ? STR_METRO_UNDERGROUND_ERR_NO_TRACK : STR_ERROR_ALREADY_BUILT);
	bool done = false;
	for (TileIndex t : TileArea(start, tile)) {
		uint64_t key = MetroUndergroundKey(t, layer);
		auto it = _metro_underground.find(key);
		if (remove) {
			if (it == _metro_underground.end() || (it->second.tracks & bit) == 0) continue;
			if (Owner{it->second.owner} != _current_company) {
				last_error = CommandCost(STR_ERROR_OWNED_BY);
				continue;
			}
			cost.AddCost(_price[Price::ClearRail] * _metro_layer_specs[layer - 1].cost_percent / 100);
			done = true;
			if (flags.Test(DoCommandFlag::Execute)) {
				it->second.tracks &= ~bit;
				if (it->second.tracks == 0) _metro_underground.erase(it);
				MarkTileDirtyByTile(t);
			}
			continue;
		}

		if (it != _metro_underground.end()) {
			if (Owner{it->second.owner} != _current_company) {
				last_error = CommandCost(STR_ERROR_OWNED_BY);
				continue;
			}
			if ((it->second.tracks & bit) != 0) continue;
		}
		CommandCost ret = MetroCheckUndergroundSurface(t, layer);
		if (ret.Failed()) {
			last_error = ret;
			continue;
		}
		cost.AddCost(GetUndergroundPiecePrice(layer));
		done = true;
		if (flags.Test(DoCommandFlag::Execute)) {
			MetroUndergroundTile &ut = _metro_underground[key];
			ut.tracks |= bit;
			ut.owner = _current_company.base();
			MarkTileDirtyByTile(t);
		}
	}
	return done ? cost : last_error;
}

/**
 * Build an entrance pavilion of the metro on a free tile.
 * @param flags type of operation
 * @param tile the tile
 * @param layer layer it leads to (used by the stations later)
 */
CommandCost CmdMetroBuildEntrance(DoCommandFlags flags, TileIndex tile, [[maybe_unused]] uint8_t layer)
{
	if (!Company::IsValidID(_current_company)) return CMD_ERROR;
	if (!IsMetroLayerAvailable(1)) return CommandCostWithParam(STR_METRO_UNDERGROUND_ERR_LAYER_CLOSED, GetMetroLayerYear(1));
	if (IsTileType(tile, TileType::Water)) return CommandCost(STR_ERROR_CAN_T_BUILD_ON_WATER);
	if (IsSteepSlope(GetTileSlope(tile))) return CommandCost(STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);
	if (!IsTileType(tile, TileType::Clear) && !IsTileType(tile, TileType::Trees)) return CommandCost(STR_METRO_UNDERGROUND_ERR_ENTRANCE_PLACE);

	CommandCost cost(ExpensesType::Construction);
	CommandCost ret = Command<Commands::LandscapeClear>::Do(flags, tile);
	if (ret.Failed()) return ret;
	cost.AddCost(ret.GetCost());
	cost.AddCost(_price[Price::BuildRail] * METRO_ENTRANCE_COST_FACTOR);
	if (GetTileSlope(tile) != SLOPE_FLAT) cost.AddCost(_price[Price::BuildFoundation]);
	if (flags.Test(DoCommandFlag::Execute)) {
		BuildObject(OBJECT_METRO_ENTRANCE, tile, _current_company, nullptr, 0);
		MarkTileDirtyByTile(tile);
	}
	return cost;
}

/* ---------------------------------------------------------------------------------------------- */
/* Drawing (simple: the chosen layer over the surface; the full underground view is step 2)       */
/* ---------------------------------------------------------------------------------------------- */

void MetroUndergroundDrawTile(const TileInfo *ti)
{
	if (_metro_underground_view_layer == 0 || _metro_underground.empty()) return;
	const MetroUndergroundTile *ut = GetMetroUndergroundTile(ti->tile, _metro_underground_view_layer);
	if (ut == nullptr) return;
	/* Dark tunnel ground over the surface, then the rails, darkened. */
	DrawGroundSpriteAt(SPR_FLAT_BARE_LAND + SlopeToSpriteOffset(ti->tileh), PALETTE_TO_TRANSPARENT, 0, 0, 0);
	const RailTypeInfo *rti = GetRailTypeInfo(RAILTYPE_RAIL);
	const SpriteID *const pieces[] = {
		&rti->base_sprites.single_x, &rti->base_sprites.single_y, &rti->base_sprites.single_n,
		&rti->base_sprites.single_s, &rti->base_sprites.single_w, &rti->base_sprites.single_e,
	};
	for (Track t : SetTrackBitIterator(static_cast<TrackBits>(ut->tracks))) {
		DrawGroundSpriteAt(*pieces[t], PALETTE_TO_GREY, 0, 0, 0);
	}
}

/* ---------------------------------------------------------------------------------------------- */
/* Construction window                                                                            */
/* ---------------------------------------------------------------------------------------------- */

/** Widgets of the #MetroUndergroundToolbarWindow. */
enum MetroUndergroundToolbarWidgets : WidgetID {
	WID_MUT_LIST, ///< Layer and tools.
	WID_MUT_INFO, ///< Hint.
};

static constexpr std::initializer_list<NWidgetPart> _nested_metro_underground_toolbar_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::DarkGreen),
		NWidget(WWT_CAPTION, Colours::DarkGreen), SetStringTip(STR_METRO_UNDERGROUND_TOOLBAR_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::DarkGreen),
		NWidget(WWT_STICKYBOX, Colours::DarkGreen),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::DarkGreen, WID_MUT_LIST), SetMinimalSize(360, 0), SetFill(1, 0), SetResize(1, 0), SetToolTip(STR_METRO_UNDERGROUND_TOOLBAR_LIST_TOOLTIP), EndContainer(),
	NWidget(WWT_PANEL, Colours::DarkGreen, WID_MUT_INFO), SetMinimalSize(360, 0), SetFill(1, 1), SetResize(1, 0), EndContainer(),
};

/** Rows of the window. */
enum MetroUndergroundRow : int {
	MUR_LAYER,  ///< Layer (click to switch); the view shows it while the window is open.
	MUR_LINE,   ///< Straight track, dragged along X or Y.
	MUR_CORNER, ///< A corner piece (click to switch which one), then a tile.
	MUR_REMOVE, ///< Remove track along a line.
	MUR_ENTRANCE, ///< Entrance pavilion.
	MUR_COUNT,
};

static const Track _metro_corner_tracks[] = { TRACK_UPPER, TRACK_RIGHT, TRACK_LOWER, TRACK_LEFT };
static const StringID _metro_corner_names[] = { STR_METRO_UNDERGROUND_CORNER_N, STR_METRO_UNDERGROUND_CORNER_E, STR_METRO_UNDERGROUND_CORNER_S, STR_METRO_UNDERGROUND_CORNER_W };

struct MetroUndergroundToolbarWindow : Window {
	int selected = -1;
	uint8_t layer = 1;
	uint8_t corner = 0;

	MetroUndergroundToolbarWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->InitNested(window_number);
		this->ShowLayer();
	}

	void Close(int data = 0) override
	{
		_metro_underground_view_layer = 0;
		MarkWholeScreenDirty();
		this->Window::Close(data);
	}

	void ShowLayer()
	{
		_metro_underground_view_layer = this->layer;
		MarkWholeScreenDirty();
	}

	std::string GetRowText(int row, bool &available) const
	{
		available = true;
		switch (row) {
			case MUR_LAYER:
				available = IsMetroLayerAvailable(this->layer);
				return GetString(available ? STR_METRO_UNDERGROUND_ROW_LAYER : STR_METRO_UNDERGROUND_ROW_LAYER_CLOSED, _metro_layer_specs[this->layer - 1].name, GetMetroLayerYear(this->layer));
			case MUR_LINE: return GetString(STR_METRO_UNDERGROUND_ROW_LINE, GetUndergroundPiecePrice(this->layer));
			case MUR_CORNER: return GetString(STR_METRO_UNDERGROUND_ROW_CORNER, _metro_corner_names[this->corner]);
			case MUR_REMOVE: return GetString(STR_METRO_UNDERGROUND_ROW_REMOVE);
			case MUR_ENTRANCE: return GetString(STR_METRO_UNDERGROUND_ROW_ENTRANCE, _price[Price::BuildRail] * METRO_ENTRANCE_COST_FACTOR);
			default: NOT_REACHED();
		}
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		switch (widget) {
			case WID_MUT_LIST:
				size.height = MUR_COUNT * GetCharacterHeight(FontSize::Normal) + padding.height;
				break;
			case WID_MUT_INFO:
				size.height = std::max<uint>(size.height, GetStringHeight(STR_METRO_UNDERGROUND_TOOLBAR_HINT, size.width - padding.width) + padding.height);
				break;
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		switch (widget) {
			case WID_MUT_LIST:
				for (int row = 0; row < MUR_COUNT; row++) {
					if (row == this->selected) {
						GfxFillRect(r.left + 1, ir.top, r.right - 1, ir.top + GetCharacterHeight(FontSize::Normal) - 1, PixelColour{GetColourGradient(Colours::DarkGreen, Shade::Lighter)});
					}
					bool available;
					std::string text = this->GetRowText(row, available);
					DrawString(ir, text, available ? TextColour::Black : TextColour::Grey);
					ir.top += GetCharacterHeight(FontSize::Normal);
				}
				break;
			case WID_MUT_INFO:
				DrawStringMultiLine(ir, STR_METRO_UNDERGROUND_TOOLBAR_HINT);
				break;
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget != WID_MUT_LIST) return;
		int row = this->GetRowFromWidget(pt.y, WID_MUT_LIST, WidgetDimensions::scaled.framerect.top, GetCharacterHeight(FontSize::Normal));
		if (row < 0 || row >= MUR_COUNT) return;
		if (row == MUR_LAYER) {
			this->layer = this->layer >= METRO_LAYER_COUNT ? 1 : this->layer + 1;
			this->ShowLayer();
			this->SetDirty();
			return;
		}
		if (row == MUR_CORNER && this->selected == MUR_CORNER) {
			this->corner = (this->corner + 1) % std::size(_metro_corner_tracks);
			this->SetDirty();
			return;
		}
		if (this->selected == row) {
			ResetObjectToPlace();
			return;
		}
		CursorID cursor = row == MUR_REMOVE ? SPR_CURSOR_DEMOLISH_FIRST : (row == MUR_ENTRANCE ? SPR_CURSOR_TRANSMITTER : SPR_CURSOR_NS_TRACK);
		SetObjectToPlaceWnd(cursor, PAL_NONE, HT_RECT, this);
		this->selected = row;
		this->SetDirty();
	}

	void OnPlaceObject([[maybe_unused]] Point pt, TileIndex tile) override
	{
		switch (this->selected) {
			case MUR_LINE: VpStartPlaceSizing(tile, VPM_X_OR_Y, DDSP_METRO_UNDERGROUND_LINE); break;
			case MUR_REMOVE: VpStartPlaceSizing(tile, VPM_X_OR_Y, DDSP_METRO_UNDERGROUND_REMOVE); break;
			case MUR_CORNER:
				Command<Commands::MetroBuildUndergroundTrack>::Post(STR_METRO_UNDERGROUND_ERR_CANT_BUILD, tile, tile, this->layer, _metro_corner_tracks[this->corner], false);
				break;
			case MUR_ENTRANCE:
				Command<Commands::MetroBuildEntrance>::Post(STR_METRO_UNDERGROUND_ERR_CANT_ENTRANCE, tile, this->layer);
				break;
			default: break;
		}
	}

	void OnPlaceDrag(ViewportPlaceMethod select_method, [[maybe_unused]] ViewportDragDropSelectionProcess select_proc, Point pt) override
	{
		VpSelectTilesWithMethod(pt.x, pt.y, select_method);
	}

	void OnPlaceMouseUp([[maybe_unused]] ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, Point pt, TileIndex start_tile, TileIndex end_tile) override
	{
		if (pt.x == -1) return;
		if (select_proc != DDSP_METRO_UNDERGROUND_LINE && select_proc != DDSP_METRO_UNDERGROUND_REMOVE) return;
		bool remove = select_proc == DDSP_METRO_UNDERGROUND_REMOVE;
		Track track = TileY(start_tile) == TileY(end_tile) && start_tile != end_tile ? TRACK_X : TRACK_Y;
		if (start_tile == end_tile) track = TRACK_X;
		Command<Commands::MetroBuildUndergroundTrack>::Post(remove ? STR_METRO_UNDERGROUND_ERR_CANT_REMOVE : STR_METRO_UNDERGROUND_ERR_CANT_BUILD, end_tile, start_tile, this->layer, track, remove);
	}

	void OnPlaceObjectAbort() override
	{
		this->selected = -1;
		this->SetDirty();
	}
};

static WindowDesc _metro_underground_toolbar_desc(__FILE__, __LINE__,
	WindowPosition::Automatic, "metro_underground_toolbar", 0, 0,
	WindowClass::MetroUndergroundToolbar, WindowClass::None,
	WindowDefaultFlag::Construction,
	_nested_metro_underground_toolbar_widgets
);

void ShowMetroUndergroundToolbar()
{
	if (!Company::IsValidID(_local_company)) return;
	AllocateWindowDescFront<MetroUndergroundToolbarWindow>(_metro_underground_toolbar_desc, 0);
}

/* ---------------------------------------------------------------------------------------------- */
/* Console                                                                                        */
/* ---------------------------------------------------------------------------------------------- */

static CompanyID GetConsoleCompany()
{
	if (Company::IsValidID(_local_company)) return _local_company;
	for (const Company *c : Company::Iterate()) return c->index;
	return CompanyID::Invalid();
}

static std::optional<TileIndex> ParseConsoleTile(std::string_view x, std::string_view y)
{
	auto tx = ParseInteger<uint>(x);
	auto ty = ParseInteger<uint>(y);
	if (!tx.has_value() || !ty.has_value() || *tx >= Map::SizeX() || *ty >= Map::SizeY()) return std::nullopt;
	return TileXY(*tx, *ty);
}

template <Commands Tcmd, typename... Targs>
static void ConsoleCommand(TileIndex tile, Targs... args)
{
	Backup<CompanyID> cur_company(_current_company, GetConsoleCompany(), FILE_LINE);
	CommandCost ret = Command<Tcmd>::Do(DoCommandFlag::Execute, tile, args...);
	cur_company.Restore();
	if (ret.Failed()) {
		IConsolePrint(CC_ERROR, "Failed: {}", GetString(ret.GetErrorMessage()));
	} else {
		IConsolePrint(CC_INFO, "Done, cost {}.", ret.GetCost());
	}
}

bool ConMetroUnderground(std::span<std::string_view> argv)
{
	if (argv.size() < 2) {
		IConsolePrint(CC_HELP, "Metro mod underground. Usage: metro_underground layers | info <x> <y> | line <x1> <y1> <x2> <y2> <layer 1-3> | remove <x1> <y1> <x2> <y2> <layer> | corner <x> <y> <layer> <0-3> | entrance <x> <y> | view <0-3> | window | count");
		return true;
	}
	std::string_view cmd = argv[1];
	if (cmd == "layers") {
		IConsolePrint(CC_DEFAULT, "first engines: electric {}, monorail {}, maglev {}", GetRailTypeFirstYear(RAILTYPE_LABEL_ELECTRIC, -1), GetRailTypeFirstYear(RAILTYPE_LABEL_MONO, -1), GetRailTypeFirstYear(RAILTYPE_LABEL_MAGLEV, -1));
		for (uint layer = 1; layer <= METRO_LAYER_COUNT; layer++) {
			IConsolePrint(CC_DEFAULT, "layer -{}: opens {}, {}", layer, GetMetroLayerYear(layer), IsMetroLayerAvailable(layer) ? "open" : "closed");
		}
		return true;
	}
	if (cmd == "count") {
		uint counts[METRO_LAYER_COUNT + 1] = {};
		for (const auto &[key, ut] : _metro_underground) counts[key & 3] += CountBits(ut.tracks);
		IConsolePrint(CC_DEFAULT, "track pieces: -1 {}, -2 {}, -3 {}; entrances {}", counts[1], counts[2], counts[3], Object::GetTypeCount(OBJECT_METRO_ENTRANCE));
		return true;
	}
	if (cmd == "window") {
		ShowMetroUndergroundToolbar();
		return true;
	}
	if (cmd == "view" && argv.size() > 2) {
		_metro_underground_view_layer = static_cast<uint8_t>(std::min<uint>(ParseInteger<uint>(argv[2]).value_or(0), METRO_LAYER_COUNT));
		MarkWholeScreenDirty();
		return true;
	}
	if (cmd == "info" && argv.size() > 3) {
		auto t = ParseConsoleTile(argv[2], argv[3]);
		if (!t.has_value()) return false;
		for (uint layer = 1; layer <= METRO_LAYER_COUNT; layer++) {
			const MetroUndergroundTile *ut = GetMetroUndergroundTile(*t, layer);
			CommandCost surface = MetroCheckUndergroundSurface(*t, layer);
			IConsolePrint(CC_DEFAULT, "layer -{}: tracks {:02x}, owner {}, may build here: {}", layer, ut != nullptr ? ut->tracks : 0, ut != nullptr ? ut->owner : 0xFF,
					surface.Succeeded() ? "yes" : GetString(surface.GetErrorMessage()));
		}
		return true;
	}
	if (cmd == "entrance" && argv.size() > 3) {
		auto t = ParseConsoleTile(argv[2], argv[3]);
		if (!t.has_value()) return false;
		ConsoleCommand<Commands::MetroBuildEntrance>(*t, static_cast<uint8_t>(1));
		return true;
	}
	if (cmd == "corner" && argv.size() > 5) {
		auto t = ParseConsoleTile(argv[2], argv[3]);
		if (!t.has_value()) return false;
		uint8_t layer = static_cast<uint8_t>(ParseInteger<uint>(argv[4]).value_or(0));
		uint corner = std::min<uint>(ParseInteger<uint>(argv[5]).value_or(0), 3);
		ConsoleCommand<Commands::MetroBuildUndergroundTrack>(*t, *t, layer, _metro_corner_tracks[corner], false);
		return true;
	}
	if ((cmd == "line" || cmd == "remove") && argv.size() > 6) {
		auto a = ParseConsoleTile(argv[2], argv[3]);
		auto b = ParseConsoleTile(argv[4], argv[5]);
		if (!a.has_value() || !b.has_value()) return false;
		uint8_t layer = static_cast<uint8_t>(ParseInteger<uint>(argv[6]).value_or(0));
		Track track = TileY(*a) == TileY(*b) && *a != *b ? TRACK_X : TRACK_Y;
		ConsoleCommand<Commands::MetroBuildUndergroundTrack>(*b, *a, layer, track, cmd == "remove");
		return true;
	}
	return false;
}
