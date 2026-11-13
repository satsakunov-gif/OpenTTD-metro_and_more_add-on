/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_dispatcher.cpp Metro mod: dispatcher, manual control of a vehicle in real time (stage 9, decisions of the user).
 *
 * The "Dispatcher" button of a vehicle window takes the vehicle under control; the main view follows it and a panel
 * shows the speed, the next station and the signal ahead. W / S set a target speed (the vehicle accelerates and
 * brakes to it), A / E / D choose left / straight / right at the next junction (trains and trams: the switch; road
 * vehicles: the turn), Q gives the choice back to the pathfinder; ships steer a course with A / D; aircraft keep
 * the game's movement at the airport and in the air but get Takeoff (leave now) and Land (nearest hangar). The
 * orders stay in force. A train can run the red signal ahead (R) for a fine of 10 rail pieces. Esc ends it. The
 * control is not saved. Numbers: table/metro_dispatcher_tables.h.
 */

#include "stdafx.h"
#include "metro_dispatcher.h"
#include "metro_dispatcher_cmd.h"
#include "aircraft.h"
#include "airport.h"
#include "command_func.h"
#include "company_func.h"
#include "console_func.h"
#include "console_type.h"
#include "core/string_consumer.hpp"
#include "economy_func.h"
#include "gfx_func.h"
#include "order_base.h"
#include "order_cmd.h"
#include "rail_map.h"
#include "roadveh.h"
#include "ship.h"
#include "station_base.h"
#include "strings_func.h"
#include "train.h"
#include "train_cmd.h"
#include "vehicle_base.h"
#include "vehicle_cmd.h"
#include "viewport_func.h"
#include "window_func.h"
#include "window_gui.h"
#include "pathfinder/follow_track.hpp"
#include "table/strings.h"
#include "table/metro_dispatcher_tables.h"

#include "safeguards.h"

/** The vehicle under control (one at a time, not saved). */
struct MetroDispatchState {
	VehicleID vehicle = VehicleID::Invalid();
	int target = -1;                              ///< Target speed in km-ish/h, -1 = no limit.
	MetroDispatchTurn turn = MetroDispatchTurn::Auto;
	int course = -1;                              ///< Ship course (#Direction), -1 = the pathfinder.
	uint fines = 0;                               ///< Red signals run.
	Money fined = 0;
};

static MetroDispatchState _dispatch;

void MetroDispatchReset()
{
	_dispatch = {};
	CloseWindowById(WindowClass::MetroDispatcher, 0);
}

bool MetroDispatchIsControlled(const Vehicle *v)
{
	return _dispatch.vehicle != VehicleID::Invalid() && v != nullptr && v->index == _dispatch.vehicle;
}

/** Target speed of the vehicle in its own speed units, or -1. */
static int TargetInternalSpeed(const Vehicle *v)
{
	if (_dispatch.target < 0) return -1;
	uint display_max = v->GetDisplayMaxSpeed();
	if (display_max == 0) return -1;
	return static_cast<int>(static_cast<int64_t>(_dispatch.target) * v->vcache.cached_max_speed / display_max);
}

int MetroDispatchAdjustSpeed(const Vehicle *front, int max_speed)
{
	if (!MetroDispatchIsControlled(front)) return max_speed;
	if (front->type == VehicleType::Aircraft && Aircraft::From(front)->state != FLYING) return max_speed; // Taxiing, take-off and landing stay the game's.
	int target = TargetInternalSpeed(front);
	return target < 0 ? max_speed : std::min(max_speed, target);
}

/** Turn from @p enterdir to @p exitdir: the dispatcher choice it matches. */
static MetroDispatchTurn TurnOf(DiagDirection enterdir, DiagDirection exitdir)
{
	switch ((to_underlying(exitdir) - to_underlying(enterdir) + 4) % 4) {
		case 0: return MetroDispatchTurn::Straight;
		case 1: return MetroDispatchTurn::Right; // Clockwise.
		case 3: return MetroDispatchTurn::Left;
		default: return MetroDispatchTurn::Auto; // Reversing is never chosen.
	}
}

static void ConsumeTurn()
{
	_dispatch.turn = MetroDispatchTurn::Auto;
	SetWindowDirty(WindowClass::MetroDispatcher, 0);
}

TrackBits MetroDispatchFilterTrainTracks(const Train *t, DiagDirection enterdir, TrackBits tracks)
{
	if (_dispatch.turn == MetroDispatchTurn::Auto || !MetroDispatchIsControlled(t) || KillFirstBit(tracks) == TRACK_BIT_NONE) return tracks;
	TrackBits chosen = TRACK_BIT_NONE;
	for (Track track : SetTrackBitIterator(tracks)) {
		if (TurnOf(enterdir, TrackdirToExitdir(TrackEnterdirToTrackdir(track, enterdir))) == _dispatch.turn) chosen |= TrackToTrackBits(track);
	}
	if (chosen == TRACK_BIT_NONE) return tracks;
	ConsumeTurn();
	return chosen;
}

Trackdir MetroDispatchChooseRoadTrackdir(const RoadVehicle *v, DiagDirection enterdir, TrackdirBits trackdirs)
{
	if (_dispatch.turn == MetroDispatchTurn::Auto || !MetroDispatchIsControlled(v->First())) return INVALID_TRACKDIR;
	for (Trackdir td : SetTrackdirBitIterator(trackdirs)) {
		if (TurnOf(enterdir, TrackdirToExitdir(td)) == _dispatch.turn) {
			ConsumeTurn();
			return td;
		}
	}
	return INVALID_TRACKDIR;
}

/** Angle between two directions in eighths of a circle (0..4). */
static uint DirAngle(Direction a, Direction b)
{
	uint d = (to_underlying(a) - to_underlying(b) + 8) % 8;
	return std::min(d, 8 - d);
}

Track MetroDispatchChooseShipTrack(const Ship *v, TrackBits tracks)
{
	if (_dispatch.course < 0 || !MetroDispatchIsControlled(v)) return INVALID_TRACK;
	Direction course = static_cast<Direction>(_dispatch.course);
	Track best = INVALID_TRACK;
	uint best_angle = UINT_MAX;
	for (Track track : SetTrackBitIterator(tracks)) {
		/* The way along the track that goes on rather than back. */
		Trackdir td = TrackToTrackdir(track);
		if (DirAngle(TrackdirToDirection(ReverseTrackdir(td)), v->direction) < DirAngle(TrackdirToDirection(td), v->direction)) td = ReverseTrackdir(td);
		uint angle = DirAngle(TrackdirToDirection(td), course);
		if (angle < best_angle) {
			best_angle = angle;
			best = track;
		}
	}
	return best;
}

/** Next signal ahead of a train: -1 none within the lookahead, 0 red, 1 green; @p tile is set to its tile. */
static int SignalAhead(const Train *t, TileIndex *signal_tile = nullptr)
{
	const Train *front = t->GetMovingFront();
	TileIndex tile = front->tile;
	Trackdir td = front->GetVehicleTrackdir();
	if (td == INVALID_TRACKDIR || !IsTileType(tile, TileType::Railway)) return -1;
	CFollowTrackRail ft(t);
	for (uint i = 0; i < METRO_DISPATCH_SIGNAL_LOOKAHEAD; i++) {
		if (!ft.Follow(tile, td)) return -1;
		tile = ft.new_tile;
		TrackdirBits bits = ft.new_td_bits;
		TrackdirBits reserved = bits & TrackBitsToTrackdirBits(GetReservedTrackbits(tile));
		td = FindFirstTrackdir(reserved != TRACKDIR_BIT_NONE ? reserved : bits);
		if (td == INVALID_TRACKDIR) return -1;
		if (IsTileType(tile, TileType::Railway) && HasSignals(tile) && HasSignalOnTrackdir(tile, td)) {
			if (signal_tile != nullptr) *signal_tile = tile;
			return GetSignalStateByTrackdir(tile, td) == SignalState::Green ? 1 : 0;
		}
	}
	return -1;
}

static Money RedSignalFine()
{
	return _price[Price::BuildRail] * METRO_DISPATCH_RED_FINE_PIECES;
}

/**
 * Manual control of a vehicle.
 * @param flags Command flags.
 * @param vehicle The vehicle (its first part).
 * @param action #MetroDispatchAction.
 * @param value Speed, turn or course of the action.
 */
CommandCost CmdMetroDispatchControl(DoCommandFlags flags, VehicleID vehicle, uint8_t action, int32_t value)
{
	Vehicle *v = Vehicle::GetIfValid(vehicle);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;
	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;
	auto a = static_cast<MetroDispatchAction>(action);
	if (a != MetroDispatchAction::Start && a != MetroDispatchAction::Stop && _dispatch.vehicle != vehicle) return CMD_ERROR;

	switch (a) {
		case MetroDispatchAction::Start:
			if (flags.Test(DoCommandFlag::Execute)) {
				_dispatch = {};
				_dispatch.vehicle = vehicle;
				_dispatch.target = -1;
			}
			return CommandCost();
		case MetroDispatchAction::Stop:
			if (flags.Test(DoCommandFlag::Execute) && _dispatch.vehicle == vehicle) _dispatch = {};
			return CommandCost();
		case MetroDispatchAction::Speed:
			if (flags.Test(DoCommandFlag::Execute)) _dispatch.target = value < 0 ? -1 : std::min<int>(value, v->GetDisplayMaxSpeed());
			return CommandCost();
		case MetroDispatchAction::Turn:
			if (value < 0 || value > to_underlying(MetroDispatchTurn::Right)) return CMD_ERROR;
			if (flags.Test(DoCommandFlag::Execute)) _dispatch.turn = static_cast<MetroDispatchTurn>(value);
			return CommandCost();
		case MetroDispatchAction::Course:
			if (v->type != VehicleType::Ship || value >= 8) return CMD_ERROR;
			if (flags.Test(DoCommandFlag::Execute)) {
				_dispatch.course = value < 0 ? -1 : value;
				Ship::From(v)->cached_path.clear();
			}
			return CommandCost();
		case MetroDispatchAction::PassRed: {
			if (v->type != VehicleType::Train) return CMD_ERROR;
			bool red = SignalAhead(Train::From(v)) == 0;
			CommandCost cost(ExpensesType::Other);
			if (red) cost.AddCost(RedSignalFine());
			if (flags.Test(DoCommandFlag::Execute)) {
				Command<Commands::ForceTrainProceed>::Do(flags, vehicle);
				if (red) {
					_dispatch.fines++;
					_dispatch.fined += RedSignalFine();
				}
			}
			return cost;
		}
		case MetroDispatchAction::Takeoff: {
			if (v->type != VehicleType::Aircraft) return CMD_ERROR;
			if (v->IsStoppedInDepot()) return Command<Commands::StartStopVehicle>::Do(flags, vehicle, false);
			if (v->current_order.IsType(OT_LOADING) && v->GetNumOrders() > 0) {
				return Command<Commands::SkipToOrder>::Do(flags, vehicle, static_cast<VehicleOrderID>((v->cur_implicit_order_index + 1) % v->GetNumOrders()));
			}
			return CommandCost(STR_METRO_DISPATCH_ERR_NOT_AT_AIRPORT);
		}
		case MetroDispatchAction::Land:
			if (v->type != VehicleType::Aircraft) return CMD_ERROR;
			return Command<Commands::SendVehicleToDepot>::Do(flags, vehicle, {}, INVALID_TILE);
		default:
			return CMD_ERROR;
	}
}

/* ---------------------------------------------------------------------------------------------- */
/* Panel                                                                                          */
/* ---------------------------------------------------------------------------------------------- */

enum MetroDispatcherWidgets : WidgetID {
	WID_MDP_CAPTION,
	WID_MDP_INFO,      ///< Speed, next station, signal, choice.
	WID_MDP_FASTER,
	WID_MDP_SLOWER,
	WID_MDP_HALT,
	WID_MDP_FREE,      ///< No speed limit.
	WID_MDP_LEFT,
	WID_MDP_STRAIGHT,
	WID_MDP_RIGHT,
	WID_MDP_AUTO,
	WID_MDP_RED,       ///< Train: pass the red signal.
	WID_MDP_TAKEOFF,   ///< Aircraft.
	WID_MDP_LAND,      ///< Aircraft.
	WID_MDP_EXIT,
};

static constexpr std::initializer_list<NWidgetPart> _nested_metro_dispatcher_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_MDP_CAPTION), SetStringTip(STR_METRO_DISPATCH_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, Colours::Grey),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey, WID_MDP_INFO), SetMinimalSize(360, 0), SetFill(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MDP_FASTER), SetFill(1, 0), SetStringTip(STR_METRO_DISPATCH_FASTER, STR_METRO_DISPATCH_FASTER_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MDP_SLOWER), SetFill(1, 0), SetStringTip(STR_METRO_DISPATCH_SLOWER, STR_METRO_DISPATCH_SLOWER_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MDP_HALT), SetFill(1, 0), SetStringTip(STR_METRO_DISPATCH_HALT, STR_METRO_DISPATCH_HALT_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MDP_FREE), SetFill(1, 0), SetStringTip(STR_METRO_DISPATCH_FREE, STR_METRO_DISPATCH_FREE_TOOLTIP),
	EndContainer(),
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_TEXTBTN, Colours::Grey, WID_MDP_LEFT), SetFill(1, 0), SetStringTip(STR_METRO_DISPATCH_LEFT, STR_METRO_DISPATCH_TURN_TOOLTIP),
		NWidget(WWT_TEXTBTN, Colours::Grey, WID_MDP_STRAIGHT), SetFill(1, 0), SetStringTip(STR_METRO_DISPATCH_STRAIGHT, STR_METRO_DISPATCH_TURN_TOOLTIP),
		NWidget(WWT_TEXTBTN, Colours::Grey, WID_MDP_RIGHT), SetFill(1, 0), SetStringTip(STR_METRO_DISPATCH_RIGHT, STR_METRO_DISPATCH_TURN_TOOLTIP),
		NWidget(WWT_TEXTBTN, Colours::Grey, WID_MDP_AUTO), SetFill(1, 0), SetStringTip(STR_METRO_DISPATCH_AUTO, STR_METRO_DISPATCH_AUTO_TOOLTIP),
	EndContainer(),
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MDP_RED), SetFill(1, 0), SetStringTip(STR_METRO_DISPATCH_RED, STR_METRO_DISPATCH_RED_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MDP_TAKEOFF), SetFill(1, 0), SetStringTip(STR_METRO_DISPATCH_TAKEOFF, STR_METRO_DISPATCH_TAKEOFF_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MDP_LAND), SetFill(1, 0), SetStringTip(STR_METRO_DISPATCH_LAND, STR_METRO_DISPATCH_LAND_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MDP_EXIT), SetFill(1, 0), SetStringTip(STR_METRO_DISPATCH_EXIT, STR_METRO_DISPATCH_EXIT_TOOLTIP),
	EndContainer(),
};

static void Control(MetroDispatchAction action, int32_t value = 0)
{
	Command<Commands::MetroDispatchControl>::Post(STR_METRO_DISPATCH_ERR, _dispatch.vehicle, to_underlying(action), value);
	SetWindowDirty(WindowClass::MetroDispatcher, 0);
}

/** Panel lines of the vehicle under control. */
static std::vector<std::string> PanelLines(const Vehicle *v)
{
	std::vector<std::string> lines;
	if (_dispatch.target < 0) {
		lines.push_back(GetString(STR_METRO_DISPATCH_SPEED_FREE, PackVelocity(v->GetDisplaySpeed(), v->type), PackVelocity(v->GetDisplayMaxSpeed(), v->type)));
	} else {
		lines.push_back(GetString(STR_METRO_DISPATCH_SPEED, PackVelocity(v->GetDisplaySpeed(), v->type), PackVelocity(_dispatch.target, v->type), PackVelocity(v->GetDisplayMaxSpeed(), v->type)));
	}
	const Station *next = nullptr;
	if (v->current_order.IsType(OT_GOTO_STATION) || v->current_order.IsType(OT_LOADING)) {
		next = Station::GetIfValid(v->current_order.IsType(OT_LOADING) ? v->last_station_visited : v->current_order.GetDestination().ToStationID());
	}
	lines.push_back(next != nullptr ? GetString(v->current_order.IsType(OT_LOADING) ? STR_METRO_DISPATCH_AT_STATION : STR_METRO_DISPATCH_NEXT_STATION, next->index) : GetString(STR_METRO_DISPATCH_NO_STATION));
	if (v->type == VehicleType::Train) {
		int signal = SignalAhead(Train::From(v));
		lines.push_back(GetString(signal < 0 ? STR_METRO_DISPATCH_SIGNAL_NONE : (signal == 0 ? STR_METRO_DISPATCH_SIGNAL_RED : STR_METRO_DISPATCH_SIGNAL_GREEN), RedSignalFine()));
	}
	if (v->type == VehicleType::Ship) {
		lines.push_back(_dispatch.course < 0 ? GetString(STR_METRO_DISPATCH_COURSE_AUTO) : GetString(STR_METRO_DISPATCH_COURSE, STR_METRO_DISPATCH_DIR_N + _dispatch.course));
	} else if (v->type != VehicleType::Aircraft) {
		static const StringID turns[] = { STR_METRO_DISPATCH_TURN_AUTO, STR_METRO_DISPATCH_TURN_LEFT, STR_METRO_DISPATCH_TURN_STRAIGHT, STR_METRO_DISPATCH_TURN_RIGHT };
		lines.push_back(GetString(STR_METRO_DISPATCH_TURN, turns[to_underlying(_dispatch.turn)]));
	}
	if (_dispatch.fines > 0) lines.push_back(GetString(STR_METRO_DISPATCH_FINES, _dispatch.fines, _dispatch.fined));
	lines.push_back(GetString(v->type == VehicleType::Ship ? STR_METRO_DISPATCH_KEYS_SHIP : (v->type == VehicleType::Aircraft ? STR_METRO_DISPATCH_KEYS_AIR : STR_METRO_DISPATCH_KEYS)));
	return lines;
}

struct MetroDispatcherWindow : Window {
	MetroDispatcherWindow(WindowDesc &desc, WindowNumber number) : Window(desc)
	{
		this->InitNested(number);
	}

	void Close(int data = 0) override
	{
		if (_dispatch.vehicle != VehicleID::Invalid()) {
			Window *main = GetMainWindow();
			if (main != nullptr && main->viewport != nullptr && main->viewport->follow_vehicle == _dispatch.vehicle) main->viewport->follow_vehicle = VehicleID::Invalid();
			if (Vehicle::IsValidID(_dispatch.vehicle)) {
				Command<Commands::MetroDispatchControl>::Post(_dispatch.vehicle, to_underlying(MetroDispatchAction::Stop), 0);
			} else {
				_dispatch = {};
			}
		}
		this->Window::Close(data);
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_MDP_CAPTION) {
			if (Vehicle::IsValidID(_dispatch.vehicle)) return GetString(STR_METRO_DISPATCH_CAPTION_VEHICLE, _dispatch.vehicle);
			return GetString(STR_METRO_DISPATCH_CAPTION);
		}
		return this->Window::GetWidgetString(widget, stringid);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget == WID_MDP_INFO) size.height = 7 * GetCharacterHeight(FontSize::Normal) + padding.height;
	}

	void OnPaint() override
	{
		const Vehicle *v = Vehicle::GetIfValid(_dispatch.vehicle);
		VehicleType type = v != nullptr ? v->type : VehicleType::Invalid;
		bool ground = type == VehicleType::Train || type == VehicleType::Road;
		this->SetWidgetsDisabledState(!ground && type != VehicleType::Ship, WID_MDP_LEFT, WID_MDP_RIGHT, WID_MDP_AUTO);
		this->SetWidgetDisabledState(WID_MDP_STRAIGHT, !ground);
		this->SetWidgetDisabledState(WID_MDP_RED, type != VehicleType::Train);
		this->SetWidgetsDisabledState(type != VehicleType::Aircraft, WID_MDP_TAKEOFF, WID_MDP_LAND);
		this->SetWidgetLoweredState(WID_MDP_LEFT, ground && _dispatch.turn == MetroDispatchTurn::Left);
		this->SetWidgetLoweredState(WID_MDP_STRAIGHT, ground && _dispatch.turn == MetroDispatchTurn::Straight);
		this->SetWidgetLoweredState(WID_MDP_RIGHT, ground && _dispatch.turn == MetroDispatchTurn::Right);
		this->SetWidgetLoweredState(WID_MDP_AUTO, type == VehicleType::Ship ? _dispatch.course < 0 : _dispatch.turn == MetroDispatchTurn::Auto);
		this->DrawWidgets();
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_MDP_INFO) return;
		const Vehicle *v = Vehicle::GetIfValid(_dispatch.vehicle);
		if (v == nullptr) return;
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		for (const std::string &line : PanelLines(v)) ir.top = DrawStringMultiLine(ir, line);
	}

	/** Steer: ships turn the course, the others choose at the next junction. */
	void Steer(int side)
	{
		const Vehicle *v = Vehicle::GetIfValid(_dispatch.vehicle);
		if (v == nullptr) return;
		if (v->type == VehicleType::Ship) {
			int course = _dispatch.course < 0 ? to_underlying(v->direction) : _dispatch.course;
			Control(MetroDispatchAction::Course, (course + side + 8) % 8);
		} else {
			Control(MetroDispatchAction::Turn, to_underlying(side < 0 ? MetroDispatchTurn::Left : MetroDispatchTurn::Right));
		}
	}

	void ChangeSpeed(int step)
	{
		const Vehicle *v = Vehicle::GetIfValid(_dispatch.vehicle);
		if (v == nullptr) return;
		int current = _dispatch.target < 0 ? v->GetDisplaySpeed() / METRO_DISPATCH_SPEED_STEP * METRO_DISPATCH_SPEED_STEP : _dispatch.target;
		Control(MetroDispatchAction::Speed, std::clamp<int>(current + step, 0, v->GetDisplayMaxSpeed()));
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_MDP_FASTER: this->ChangeSpeed(METRO_DISPATCH_SPEED_STEP); break;
			case WID_MDP_SLOWER: this->ChangeSpeed(-METRO_DISPATCH_SPEED_STEP); break;
			case WID_MDP_HALT: Control(MetroDispatchAction::Speed, 0); break;
			case WID_MDP_FREE: Control(MetroDispatchAction::Speed, -1); break;
			case WID_MDP_LEFT: this->Steer(-1); break;
			case WID_MDP_RIGHT: this->Steer(1); break;
			case WID_MDP_STRAIGHT: Control(MetroDispatchAction::Turn, to_underlying(MetroDispatchTurn::Straight)); break;
			case WID_MDP_AUTO: {
				const Vehicle *v = Vehicle::GetIfValid(_dispatch.vehicle);
				if (v != nullptr && v->type == VehicleType::Ship) {
					Control(MetroDispatchAction::Course, -1);
				} else {
					Control(MetroDispatchAction::Turn, to_underlying(MetroDispatchTurn::Auto));
				}
				break;
			}
			case WID_MDP_RED: Control(MetroDispatchAction::PassRed); break;
			case WID_MDP_TAKEOFF: Control(MetroDispatchAction::Takeoff); break;
			case WID_MDP_LAND: Control(MetroDispatchAction::Land); break;
			case WID_MDP_EXIT: this->Close(); break;
		}
	}

	EventState OnKeyPress([[maybe_unused]] char32_t key, uint16_t keycode) override
	{
		switch (keycode) {
			case 'W': case WKC_UP: this->OnClick({}, WID_MDP_FASTER, 1); break;
			case 'S': case WKC_DOWN: this->OnClick({}, WID_MDP_SLOWER, 1); break;
			case WKC_SPACE: this->OnClick({}, WID_MDP_HALT, 1); break;
			case 'F': this->OnClick({}, WID_MDP_FREE, 1); break;
			case 'A': case WKC_LEFT: this->OnClick({}, WID_MDP_LEFT, 1); break;
			case 'D': case WKC_RIGHT: this->OnClick({}, WID_MDP_RIGHT, 1); break;
			case 'E': this->OnClick({}, WID_MDP_STRAIGHT, 1); break;
			case 'Q': this->OnClick({}, WID_MDP_AUTO, 1); break;
			case 'R': this->OnClick({}, WID_MDP_RED, 1); break;
			case 'T': this->OnClick({}, WID_MDP_TAKEOFF, 1); break;
			case 'L': this->OnClick({}, WID_MDP_LAND, 1); break;
			case WKC_ESC: this->Close(); break;
			default: return EventState::NotHandled;
		}
		return EventState::Handled;
	}

	void OnRealtimeTick([[maybe_unused]] uint delta_ms) override
	{
		if (!Vehicle::IsValidID(_dispatch.vehicle)) {
			this->Close();
			return;
		}
		this->SetWidgetDirty(WID_MDP_INFO);
	}
};

static WindowDesc _metro_dispatcher_desc(__FILE__, __LINE__,
	WindowPosition::Automatic, "metro_dispatcher", 0, 0,
	WindowClass::MetroDispatcher, WindowClass::None,
	{},
	_nested_metro_dispatcher_widgets
);

void MetroDispatchStart(VehicleID vehicle)
{
	Vehicle *v = Vehicle::GetIfValid(vehicle);
	if (v == nullptr) return;
	v = v->First();
	if (_dispatch.vehicle != VehicleID::Invalid() && _dispatch.vehicle != v->index) CloseWindowById(WindowClass::MetroDispatcher, 0);
	if (!Command<Commands::MetroDispatchControl>::Post(STR_METRO_DISPATCH_ERR, v->index, to_underlying(MetroDispatchAction::Start), 0)) return;
	Window *main = GetMainWindow();
	if (main != nullptr && main->viewport != nullptr) {
		ScrollMainWindowTo(v->x_pos, v->y_pos, v->z_pos, true);
		main->viewport->follow_vehicle = v->index;
	}
	if (FindWindowById(WindowClass::MetroDispatcher, 0) == nullptr) new MetroDispatcherWindow(_metro_dispatcher_desc, 0);
	SetWindowDirty(WindowClass::MetroDispatcher, 0);
}

/** Console "metro_dispatch [start <vehicle> | stop | speed <km/h|-1> | turn <0-3> | course <0-7|-1> | red | takeoff | land | status]". */
bool ConMetroDispatch(std::span<std::string_view> argv)
{
	if (argv.size() < 2) {
		IConsolePrint(CC_HELP, "Metro mod dispatcher. Usage: metro_dispatch start <vehicle> | stop | speed <km/h|-1> | turn <0 auto,1 left,2 straight,3 right> | course <0-7|-1> | red | takeoff | land | status");
		return true;
	}
	std::string_view cmd = argv[1];
	int32_t value = argv.size() > 2 ? ParseInteger<int32_t>(argv[2]).value_or(0) : 0;
	auto control = [&](MetroDispatchAction a) {
		CommandCost ret = Command<Commands::MetroDispatchControl>::Do(DoCommandFlag::Execute, _dispatch.vehicle, to_underlying(a), value);
		if (ret.Succeeded() && ret.GetCost() != 0) SubtractMoneyFromCompany(_local_company, ret);
		IConsolePrint(ret.Succeeded() ? CC_INFO : CC_ERROR, "{}: {}, cost {}", cmd, ret.Succeeded() ? "done" : "failed", GetString(STR_JUST_CURRENCY_LONG, ret.GetCost()));
	};
	if (cmd == "start" && argv.size() > 2) {
		MetroDispatchStart(VehicleID(static_cast<uint32_t>(value)));
	} else if (cmd == "stop") {
		CloseWindowById(WindowClass::MetroDispatcher, 0);
		_dispatch = {};
	} else if (cmd == "speed") {
		control(MetroDispatchAction::Speed);
	} else if (cmd == "turn") {
		control(MetroDispatchAction::Turn);
	} else if (cmd == "course") {
		control(MetroDispatchAction::Course);
	} else if (cmd == "red") {
		control(MetroDispatchAction::PassRed);
	} else if (cmd == "takeoff") {
		control(MetroDispatchAction::Takeoff);
	} else if (cmd == "land") {
		control(MetroDispatchAction::Land);
	}
	const Vehicle *v = Vehicle::GetIfValid(_dispatch.vehicle);
	if (v == nullptr) {
		IConsolePrint(CC_INFO, "dispatcher: no vehicle");
		return true;
	}
	IConsolePrint(CC_INFO, "dispatcher: vehicle {} '{}', speed {} (internal {}), target {} (internal {}), turn {}, course {}, fines {}",
			v->index, GetString(STR_VEHICLE_NAME, v->index), v->GetDisplaySpeed(), v->cur_speed, _dispatch.target, TargetInternalSpeed(v), to_underlying(_dispatch.turn), _dispatch.course, _dispatch.fines);
	if (v->type == VehicleType::Train) {
		TileIndex tile = INVALID_TILE;
		int signal = SignalAhead(Train::From(v), &tile);
		IConsolePrint(CC_INFO, "signal ahead: {} at {}", signal, tile == INVALID_TILE ? std::string("-") : fmt::format("({}, {})", TileX(tile), TileY(tile)));
	}
	return true;
}
