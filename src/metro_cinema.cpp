/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_cinema.cpp Metro mod: cinema camera (stage 9, decisions of the user).
 *
 * The camera smoothly follows a vehicle, a private car or a pedestrian of the towns. Modes: "behind" (the camera trails
 * the target along its motion), "orbit" (it circles the target 4..6 tiles away) and "random" (every 10..20 seconds a
 * new interesting target: a fast train, an underground train, a car in a jam, the crowd at a stadium on match day, an
 * aircraft taking off or landing; each with behind or orbit). It starts from the "Cinema" button of a vehicle window
 * (that vehicle) or Ctrl+K (random). H hides the interface (a full screen view over all windows), Esc ends it.
 * Everything is on the user interface side: nothing is saved and the game is not touched. Numbers:
 * table/metro_cinema_tables.h.
 */

#include "stdafx.h"
#include "metro_cinema.h"
#include "metro_city.h"
#include "metro_overlay.h"
#include "metro_traffic.h"
#include "metro_underground.h"
#include "aircraft.h"
#include "airport.h"
#include "train.h"
#include "town.h"
#include "vehicle_base.h"
#include "landscape.h"
#include "console_func.h"
#include "console_type.h"
#include "gfx_func.h"
#include "strings_func.h"
#include "viewport_func.h"
#include "window_func.h"
#include "window_gui.h"
#include "zoom_func.h"
#include "core/random_func.hpp"
#include "core/string_consumer.hpp"
#include "table/strings.h"
#include "table/metro_cinema_tables.h"

#include <cmath>

#include "safeguards.h"

/** Mode of the camera. */
enum class CinemaMode : uint8_t {
	Behind,
	Orbit,
	Random,
};

/** What the camera follows. */
enum class CinemaTarget : uint8_t {
	None,
	Vehicle,
	Car,
	Pedestrian,
	Point, ///< A fixed tile (the stadium on match day).
};

static bool _cinema_active = false;
static CinemaMode _cinema_mode = CinemaMode::Random;
static CinemaMode _cinema_shot = CinemaMode::Behind;   ///< Behind or orbit of the current target in the random mode.
static CinemaTarget _cinema_target = CinemaTarget::None;
static VehicleID _cinema_vehicle = VehicleID::Invalid();
static uint32_t _cinema_actor = 0;
static TileIndex _cinema_point = INVALID_TILE;
static uint16_t _cinema_town = 0;                       ///< Town of a car, pedestrian or stadium.
static double _cam_x = 0, _cam_y = 0, _cam_z = 0;       ///< Where the camera looks now (world pixels).
static bool _cam_valid = false;
static double _orbit_phase = 0;                         ///< Angle of the orbit, radians.
static uint32_t _orbit_time = 0;                        ///< For the breathing radius.
static int32_t _switch_ms = 0;                          ///< Random mode: time left on the current target.
static bool _cinema_hidden = false;                     ///< The interface is hidden (full screen view).
static uint32_t _hint_ms = 0;                           ///< Time left to show the key hint on the full screen view.
static bool _cinema_turned_underground = false;         ///< The camera switched the underground view on.

bool MetroCinemaIsActive()
{
	return _cinema_active;
}

/* ---------------------------------------------------------------------------------------------- */
/* Targets                                                                                        */
/* ---------------------------------------------------------------------------------------------- */

/** Position of the target in world pixels and its direction of motion; false if it is gone. */
static bool GetTargetPosition(double &x, double &y, double &z, int &dx, int &dy)
{
	dx = dy = 0;
	switch (_cinema_target) {
		case CinemaTarget::Vehicle: {
			const Vehicle *v = Vehicle::GetIfValid(_cinema_vehicle);
			if (v == nullptr || v->vehstatus.Test(VehState::Crashed)) return false;
			const Vehicle *front = v->GetMovingFront();
			x = front->x_pos;
			y = front->y_pos;
			z = front->z_pos;
			uint d = to_underlying(front->direction) & 7;
			dx = _metro_cinema_dir_vec[d][0];
			dy = _metro_cinema_dir_vec[d][1];
			return true;
		}
		case CinemaTarget::Car:
		case CinemaTarget::Pedestrian: {
			MetroTrafficActor actor;
			if (!MetroTrafficFindActor(_cinema_actor, actor)) return false;
			x = actor.x;
			y = actor.y;
			z = GetSlopePixelZ(actor.x, actor.y);
			TileIndexDiffC d = TileIndexDiffCByDiagDir(actor.dir);
			dx = d.x;
			dy = d.y;
			return true;
		}
		case CinemaTarget::Point:
			if (_cinema_point == INVALID_TILE) return false;
			if (MetroCityGetMatchDayStadium(TownID(_cinema_town)) == INVALID_TILE && _cinema_mode == CinemaMode::Random) return false; // The match is over.
			x = TileX(_cinema_point) * TILE_SIZE + TILE_SIZE;
			y = TileY(_cinema_point) * TILE_SIZE + TILE_SIZE;
			z = TilePixelHeight(_cinema_point);
			return true;
		default:
			return false;
	}
}

static bool IsMoving(const Vehicle *v)
{
	return v->IsPrimaryVehicle() && !v->vehstatus.Any({VehState::Hidden, VehState::Stopped, VehState::Crashed}) && v->cur_speed > 0;
}

/** Choose a new interesting target for the random mode; false if there is nothing to follow at all. */
static bool PickRandomTarget()
{
	std::vector<const Vehicle *> fast, underground, aircraft, any;
	for (const Vehicle *v : Vehicle::Iterate()) {
		if (!IsMoving(v)) continue;
		any.push_back(v);
		if (v->type == VehicleType::Train) {
			if (MetroIsUndergroundVehicleTile(v->tile)) {
				underground.push_back(v);
			} else {
				fast.push_back(v);
			}
		}
		if (v->type == VehicleType::Aircraft) {
			uint8_t state = Aircraft::From(v)->state;
			if (state == TAKEOFF || state == STARTTAKEOFF || state == ENDTAKEOFF || state == LANDING || state == ENDLANDING) aircraft.push_back(v);
		}
	}
	std::sort(fast.begin(), fast.end(), [](const Vehicle *a, const Vehicle *b) { return a->cur_speed > b->cur_speed; });
	if (fast.size() > METRO_CINEMA_FAST_TRAINS) fast.resize(METRO_CINEMA_FAST_TRAINS);

	std::vector<MetroTrafficActor> jammed, walkers;
	for (const MetroTrafficActor &a : MetroTrafficListActors()) {
		if (a.pedestrian) {
			walkers.push_back(a);
		} else if (a.jammed) {
			jammed.push_back(a);
		}
	}
	std::vector<std::pair<TileIndex, uint16_t>> stadiums;
	for (const Town *t : Town::Iterate()) {
		TileIndex stadium = MetroCityGetMatchDayStadium(t->index);
		if (stadium != INVALID_TILE) stadiums.emplace_back(stadium, t->index.base());
	}

	/* A random kind among those that have something, then a random one of it. */
	enum Kind : uint8_t { Fast, Underground, Jam, Stadium, Plane, Walker, Any, KindEnd };
	std::vector<Kind> kinds;
	if (!fast.empty()) kinds.push_back(Fast);
	if (!underground.empty()) kinds.push_back(Underground);
	if (!jammed.empty()) kinds.push_back(Jam);
	if (!stadiums.empty()) kinds.push_back(Stadium);
	if (!aircraft.empty()) kinds.push_back(Plane);
	if (!walkers.empty()) kinds.push_back(Walker);
	if (kinds.empty() && !any.empty()) kinds.push_back(Any);
	if (kinds.empty()) return false;

	auto pick_vehicle = [](const std::vector<const Vehicle *> &list) {
		_cinema_target = CinemaTarget::Vehicle;
		_cinema_vehicle = list[InteractiveRandomRange(static_cast<uint32_t>(list.size()))]->index;
	};
	Kind kind = kinds[InteractiveRandomRange(static_cast<uint32_t>(kinds.size()))];
	switch (kind) {
		case Fast: pick_vehicle(fast); break;
		case Underground: pick_vehicle(underground); break;
		case Plane: pick_vehicle(aircraft); break;
		case Any: pick_vehicle(any); break;
		case Jam:
		case Walker: {
			const std::vector<MetroTrafficActor> &chosen = kind == Walker ? walkers : jammed;
			const MetroTrafficActor &a = chosen[InteractiveRandomRange(static_cast<uint32_t>(chosen.size()))];
			_cinema_target = a.pedestrian ? CinemaTarget::Pedestrian : CinemaTarget::Car;
			_cinema_actor = a.id;
			_cinema_town = a.town;
			break;
		}
		case Stadium: {
			const auto &[tile, town] = stadiums[InteractiveRandomRange(static_cast<uint32_t>(stadiums.size()))];
			_cinema_target = CinemaTarget::Point;
			_cinema_point = tile;
			_cinema_town = town;
			break;
		}
		default: NOT_REACHED();
	}
	_cinema_shot = (_cinema_target == CinemaTarget::Point || InteractiveRandomRange(2) == 0) ? CinemaMode::Orbit : CinemaMode::Behind;
	_switch_ms = static_cast<int32_t>(METRO_CINEMA_SWITCH_MIN_MS + InteractiveRandomRange(METRO_CINEMA_SWITCH_MAX_MS - METRO_CINEMA_SWITCH_MIN_MS + 1));
	_orbit_phase = InteractiveRandomRange(628) / 100.0;
	SetWindowDirty(WindowClass::MetroCinema, 0);
	return true;
}

/** Text of the current target for the panel. */
static std::string TargetText()
{
	switch (_cinema_target) {
		case CinemaTarget::Vehicle:
			if (Vehicle::IsValidID(_cinema_vehicle)) return GetString(STR_METRO_CINEMA_TARGET_VEHICLE, _cinema_vehicle);
			break;
		case CinemaTarget::Car:
			if (Town::IsValidID(_cinema_town)) return GetString(STR_METRO_CINEMA_TARGET_CAR, _cinema_town);
			break;
		case CinemaTarget::Pedestrian:
			if (Town::IsValidID(_cinema_town)) return GetString(STR_METRO_CINEMA_TARGET_PEDESTRIAN, _cinema_town);
			break;
		case CinemaTarget::Point:
			if (Town::IsValidID(_cinema_town)) return GetString(STR_METRO_CINEMA_TARGET_STADIUM, _cinema_town);
			break;
		default:
			break;
	}
	return GetString(STR_METRO_CINEMA_TARGET_NONE);
}

/* ---------------------------------------------------------------------------------------------- */
/* Camera                                                                                         */
/* ---------------------------------------------------------------------------------------------- */

/** Aim the viewport of a window at the world point. */
static void AimViewport(Window *w, double x, double y, double z)
{
	if (w == nullptr || w->viewport == nullptr) return;
	w->viewport->follow_vehicle = VehicleID::Invalid();
	Point pt = RemapCoords(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z));
	w->viewport->dest_scrollpos_x = pt.x - w->viewport->virtual_width / 2;
	w->viewport->dest_scrollpos_y = pt.y - w->viewport->virtual_height / 2;
}

/** Underground trains are seen only in the underground view: switch it on for them and back after. */
static void UpdateUndergroundView()
{
	bool want = false;
	if (_cinema_target == CinemaTarget::Vehicle) {
		const Vehicle *v = Vehicle::GetIfValid(_cinema_vehicle);
		want = v != nullptr && MetroIsUndergroundVehicleTile(v->GetMovingFront()->tile);
	}
	if (want && !_metro_underground_view) {
		MetroToggleUndergroundView();
		_cinema_turned_underground = true;
	} else if (!want && _cinema_turned_underground) {
		if (_metro_underground_view) MetroToggleUndergroundView();
		_cinema_turned_underground = false;
	}
}

/** One frame of the camera. */
static void UpdateCamera(uint delta_ms)
{
	if (_cinema_mode == CinemaMode::Random) {
		_switch_ms -= static_cast<int32_t>(delta_ms);
		if (_switch_ms <= 0 || _cinema_target == CinemaTarget::None) PickRandomTarget();
	}

	double x, y, z;
	int dx, dy;
	if (!GetTargetPosition(x, y, z, dx, dy)) {
		/* The target is gone (sold, crashed, a car arrived home): find another one. */
		if (!PickRandomTarget() || !GetTargetPosition(x, y, z, dx, dy)) {
			_cinema_target = CinemaTarget::None;
			return;
		}
	}
	UpdateUndergroundView();

	CinemaMode shot = _cinema_mode == CinemaMode::Random ? _cinema_shot : _cinema_mode;
	double aim_x = x, aim_y = y;
	if (shot == CinemaMode::Behind) {
		double len = std::sqrt(static_cast<double>(dx * dx + dy * dy));
		if (len > 0) {
			aim_x -= dx / len * METRO_CINEMA_BEHIND_PX;
			aim_y -= dy / len * METRO_CINEMA_BEHIND_PX;
		}
	} else {
		_orbit_phase += 2 * M_PI * delta_ms / METRO_CINEMA_ORBIT_PERIOD_MS;
		if (_orbit_phase > 2 * M_PI) _orbit_phase -= 2 * M_PI;
		_orbit_time += delta_ms;
		double breath = 0.5 + 0.5 * std::sin(2 * M_PI * (_orbit_time % METRO_CINEMA_ORBIT_BREATH_MS) / METRO_CINEMA_ORBIT_BREATH_MS);
		double radius = METRO_CINEMA_ORBIT_MIN_PX + (METRO_CINEMA_ORBIT_MAX_PX - METRO_CINEMA_ORBIT_MIN_PX) * breath;
		aim_x += std::cos(_orbit_phase) * radius;
		aim_y += std::sin(_orbit_phase) * radius;
	}

	double dist = std::hypot(aim_x - _cam_x, aim_y - _cam_y);
	if (!_cam_valid || dist > METRO_CINEMA_JUMP_PX) {
		_cam_x = aim_x;
		_cam_y = aim_y;
		_cam_z = z;
		_cam_valid = true;
	} else {
		double a = 1.0 - std::exp(-static_cast<double>(delta_ms) / METRO_CINEMA_SMOOTH_MS);
		_cam_x += (aim_x - _cam_x) * a;
		_cam_y += (aim_y - _cam_y) * a;
		_cam_z += (z - _cam_z) * a;
	}

	AimViewport(GetMainWindow(), _cam_x, _cam_y, _cam_z);
	AimViewport(FindWindowById(WindowClass::MetroCinemaView, 0), _cam_x, _cam_y, _cam_z);
}

/* ---------------------------------------------------------------------------------------------- */
/* Windows                                                                                        */
/* ---------------------------------------------------------------------------------------------- */

/** Widgets of the cinema panel. */
enum MetroCinemaWidgets : WidgetID {
	WID_MCN_BEHIND, ///< Mode "behind".
	WID_MCN_ORBIT,  ///< Mode "orbit".
	WID_MCN_RANDOM, ///< Mode "random".
	WID_MCN_TARGET, ///< What the camera follows.
	WID_MCN_NEXT,   ///< Another random target.
	WID_MCN_HIDE,   ///< Hide the interface.
	WID_MCN_EXIT,   ///< End the cinema.
};

/** Widgets of the full screen view. */
enum MetroCinemaViewWidgets : WidgetID {
	WID_MCNV_VIEWPORT, ///< The view.
};

static void SetCinemaHidden(bool hidden);

/** Keys of the cinema in both windows. */
static EventState HandleCinemaKey(uint16_t keycode)
{
	switch (keycode) {
		case WKC_ESC: MetroCinemaStop(); return EventState::Handled;
		case 'H': SetCinemaHidden(!_cinema_hidden); return EventState::Handled;
		case '1': _cinema_mode = CinemaMode::Behind; break;
		case '2': _cinema_mode = CinemaMode::Orbit; break;
		case '3': _cinema_mode = CinemaMode::Random; _switch_ms = 0; break;
		default: return EventState::NotHandled;
	}
	SetWindowDirty(WindowClass::MetroCinema, 0);
	return EventState::Handled;
}

static constexpr std::initializer_list<NWidgetPart> _nested_metro_cinema_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey), SetStringTip(STR_METRO_CINEMA_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, Colours::Grey),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey),
		NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
			NWidget(WWT_TEXTBTN, Colours::Grey, WID_MCN_BEHIND), SetFill(1, 0), SetStringTip(STR_METRO_CINEMA_BEHIND, STR_METRO_CINEMA_BEHIND_TOOLTIP),
			NWidget(WWT_TEXTBTN, Colours::Grey, WID_MCN_ORBIT), SetFill(1, 0), SetStringTip(STR_METRO_CINEMA_ORBIT, STR_METRO_CINEMA_ORBIT_TOOLTIP),
			NWidget(WWT_TEXTBTN, Colours::Grey, WID_MCN_RANDOM), SetFill(1, 0), SetStringTip(STR_METRO_CINEMA_RANDOM, STR_METRO_CINEMA_RANDOM_TOOLTIP),
		EndContainer(),
		NWidget(WWT_TEXT, Colours::Invalid, WID_MCN_TARGET), SetMinimalSize(300, 12), SetFill(1, 0), SetPadding(2, 4, 2, 4),
		NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
			NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MCN_NEXT), SetFill(1, 0), SetStringTip(STR_METRO_CINEMA_NEXT, STR_METRO_CINEMA_NEXT_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MCN_HIDE), SetFill(1, 0), SetStringTip(STR_METRO_CINEMA_HIDE, STR_METRO_CINEMA_HIDE_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MCN_EXIT), SetFill(1, 0), SetStringTip(STR_METRO_CINEMA_EXIT, STR_METRO_CINEMA_EXIT_TOOLTIP),
		EndContainer(),
	EndContainer(),
};

/** The panel of the cinema: modes, target, hide and exit. It runs the camera every frame. */
struct MetroCinemaWindow : Window {
	MetroCinemaWindow(WindowDesc &desc, WindowNumber number) : Window(desc)
	{
		this->InitNested(number);
	}

	void Close(int data = 0) override
	{
		if (_cinema_active) MetroCinemaStop();
		this->Window::Close(data);
	}

	void OnPaint() override
	{
		this->SetWidgetLoweredState(WID_MCN_BEHIND, _cinema_mode == CinemaMode::Behind);
		this->SetWidgetLoweredState(WID_MCN_ORBIT, _cinema_mode == CinemaMode::Orbit);
		this->SetWidgetLoweredState(WID_MCN_RANDOM, _cinema_mode == CinemaMode::Random);
		this->DrawWidgets();
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_MCN_TARGET) return TargetText();
		return this->Window::GetWidgetString(widget, stringid);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_MCN_BEHIND: HandleCinemaKey('1'); break;
			case WID_MCN_ORBIT: HandleCinemaKey('2'); break;
			case WID_MCN_RANDOM: HandleCinemaKey('3'); break;
			case WID_MCN_NEXT: PickRandomTarget(); this->SetDirty(); break;
			case WID_MCN_HIDE: SetCinemaHidden(true); break;
			case WID_MCN_EXIT: MetroCinemaStop(); break;
		}
	}

	EventState OnKeyPress([[maybe_unused]] char32_t key, uint16_t keycode) override
	{
		return HandleCinemaKey(keycode);
	}

	void OnRealtimeTick(uint delta_ms) override
	{
		if (!_cinema_active) return;
		CinemaTarget before = _cinema_target;
		VehicleID vehicle = _cinema_vehicle;
		UpdateCamera(std::min<uint>(delta_ms, 200));
		if (_hint_ms > 0) {
			_hint_ms = _hint_ms > delta_ms ? _hint_ms - delta_ms : 0;
			if (_hint_ms == 0) SetWindowDirty(WindowClass::MetroCinemaView, 0);
		}
		if (before != _cinema_target || vehicle != _cinema_vehicle) this->SetWidgetDirty(WID_MCN_TARGET);
	}
};

static WindowDesc _metro_cinema_desc(__FILE__, __LINE__,
	WindowPosition::Automatic, "metro_cinema", 0, 0,
	WindowClass::MetroCinema, WindowClass::None,
	{},
	_nested_metro_cinema_widgets
);

static constexpr std::initializer_list<NWidgetPart> _nested_metro_cinema_view_widgets = {
	NWidget(NWID_VIEWPORT, Colours::Invalid, WID_MCNV_VIEWPORT), SetMinimalSize(64, 64),
};

/** Full screen view over all windows while the interface is hidden. */
struct MetroCinemaViewWindow : Window {
	MetroCinemaViewWindow(WindowDesc &desc, WindowNumber number) : Window(desc)
	{
		this->InitNested(number);
		const Window *main = GetMainWindow();
		NWidgetViewport *nvp = this->GetWidget<NWidgetViewport>(WID_MCNV_VIEWPORT);
		nvp->InitializeViewport(this, TileXY(Map::MaxX() / 2, Map::MaxY() / 2).base(), main->viewport->zoom);
		this->viewport->scrollpos_x = main->viewport->scrollpos_x + (main->viewport->virtual_width - this->viewport->virtual_width) / 2;
		this->viewport->scrollpos_y = main->viewport->scrollpos_y + (main->viewport->virtual_height - this->viewport->virtual_height) / 2;
		this->viewport->dest_scrollpos_x = this->viewport->scrollpos_x;
		this->viewport->dest_scrollpos_y = this->viewport->scrollpos_y;
		this->viewport->map_type = main->viewport->map_type;
	}

	Point OnInitialPosition([[maybe_unused]] int16_t sm_width, [[maybe_unused]] int16_t sm_height, [[maybe_unused]] int window_number) override
	{
		return {0, 0};
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget == WID_MCNV_VIEWPORT) size = {static_cast<uint>(_screen.width), static_cast<uint>(_screen.height)};
	}

	void OnPaint() override
	{
		this->DrawWidgets();
		if (_hint_ms > 0) {
			int pad = ScaleGUITrad(8);
			DrawString(pad, _screen.width - pad, pad, STR_METRO_CINEMA_HINT, TextColour::White, AlignmentH::Start, false, FontSize::Normal);
			DrawString(pad, _screen.width - pad, pad + GetCharacterHeight(FontSize::Normal) + 2, TargetText(), TextColour::Yellow, AlignmentH::Start, false, FontSize::Normal);
		}
	}

	EventState OnKeyPress([[maybe_unused]] char32_t key, uint16_t keycode) override
	{
		return HandleCinemaKey(keycode);
	}

	void OnMouseWheel(int wheel, [[maybe_unused]] WidgetID widget) override
	{
		ZoomInOrOutToCursorWindow(wheel < 0, this);
	}
};

static WindowDesc _metro_cinema_view_desc(__FILE__, __LINE__,
	WindowPosition::Manual, nullptr, 0, 0,
	WindowClass::MetroCinemaView, WindowClass::None,
	{WindowDefaultFlag::NoFocus},
	_nested_metro_cinema_view_widgets
);

static void ShowCinemaView()
{
	CloseWindowById(WindowClass::MetroCinemaView, 0);
	new MetroCinemaViewWindow(_metro_cinema_view_desc, 0);
}

static void SetCinemaHidden(bool hidden)
{
	_cinema_hidden = hidden;
	if (hidden) {
		_hint_ms = METRO_CINEMA_HINT_MS;
		ShowCinemaView();
	} else {
		CloseWindowById(WindowClass::MetroCinemaView, 0);
	}
}

/* ---------------------------------------------------------------------------------------------- */
/* Start and stop                                                                                 */
/* ---------------------------------------------------------------------------------------------- */

/** Start the cinema camera on a vehicle, or with random targets for an invalid one. */
void MetroCinemaStart(VehicleID vehicle)
{
	_cinema_active = true;
	_cam_valid = false;
	_cinema_hidden = false;
	_orbit_time = 0;
	if (Vehicle::IsValidID(vehicle)) {
		_cinema_mode = CinemaMode::Behind;
		_cinema_target = CinemaTarget::Vehicle;
		_cinema_vehicle = Vehicle::Get(vehicle)->First()->index;
	} else {
		_cinema_mode = CinemaMode::Random;
		_cinema_target = CinemaTarget::None;
		_switch_ms = 0;
	}
	Window *w = FindWindowById(WindowClass::MetroCinema, 0);
	if (w == nullptr) {
		new MetroCinemaWindow(_metro_cinema_desc, 0);
	} else {
		w->SetDirty();
	}
}

void MetroCinemaStop()
{
	if (!_cinema_active) return;
	_cinema_active = false;
	if (_cinema_turned_underground && _metro_underground_view) MetroToggleUndergroundView();
	_cinema_turned_underground = false;
	_cinema_target = CinemaTarget::None;
	CloseWindowById(WindowClass::MetroCinemaView, 0);
	CloseWindowById(WindowClass::MetroCinema, 0);
}

/** Console "metro_cinema [start [vehicle] | stop | mode <0-2> | hide | status]". */
bool ConMetroCinema(std::span<std::string_view> argv)
{
	if (argv.size() < 2) {
		IConsolePrint(CC_HELP, "Metro mod cinema camera. Usage: metro_cinema start [vehicle] | stop | mode <0 behind, 1 orbit, 2 random> | next | hide | status");
		return true;
	}
	std::string_view cmd = argv[1];
	if (cmd == "start") {
		VehicleID v = argv.size() > 2 ? VehicleID(ParseInteger<uint>(argv[2]).value_or(VehicleID::Invalid().base())) : VehicleID::Invalid();
		MetroCinemaStart(v);
	} else if (cmd == "stop") {
		MetroCinemaStop();
	} else if (cmd == "mode" && argv.size() > 2) {
		HandleCinemaKey(static_cast<uint16_t>('1' + std::min<uint>(ParseInteger<uint>(argv[2]).value_or(2), 2)));
	} else if (cmd == "next") {
		PickRandomTarget();
	} else if (cmd == "hide") {
		if (_cinema_active) SetCinemaHidden(!_cinema_hidden);
	} else if (cmd == "frame") {
		/* Advance the camera without a screen (tests): frame <ms>. */
		if (_cinema_active) UpdateCamera(argv.size() > 2 ? ParseInteger<uint>(argv[2]).value_or(30) : 30);
	}
	IConsolePrint(CC_INFO, "cinema: active {}, mode {}, shot {}, target {} '{}', camera ({:.0f}, {:.0f}, {:.0f}), next switch in {} ms, hidden {}, underground view by camera {}",
			_cinema_active, to_underlying(_cinema_mode), to_underlying(_cinema_shot), to_underlying(_cinema_target), TargetText(), _cam_x / TILE_SIZE, _cam_y / TILE_SIZE, _cam_z, _switch_ms, _cinema_hidden, _cinema_turned_underground);
	return true;
}
