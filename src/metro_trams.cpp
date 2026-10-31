/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_trams.cpp Metro mod: built-in trams by era (1950s, 1970s, 2000s, 2040s).
 *
 * The trams are engines in the scope #METRO_TRAM_GRFID of the engine ID mapping, as if a NewGRF defined them: games
 * and saves without them get them appended, the vanilla engines and NewGRF sets keep their IDs. Their data come from
 * #_metro_trams after every NewGRF load.
 */

#include "stdafx.h"
#include "metro_trams.h"
#include "engine_base.h"
#include "engine_func.h"
#include "engine_override.h"
#include "date_func.h"
#include "company_base.h"
#include "company_func.h"
#include "console_func.h"
#include "console_type.h"
#include "strings_func.h"
#include "command_func.h"
#include "road_cmd.h"
#include "road_map.h"
#include "town.h"
#include "core/backup_type.hpp"
#include "core/string_consumer.hpp"
#include "newgrf/newgrf_internal_vehicle.h"
#include "table/strings.h"
#include "table/metro_tram_tables.h"

#include "safeguards.h"

bool IsMetroTramEngineScope(GrfID grfid)
{
	return grfid == METRO_TRAM_GRFID;
}

/** Create the built-in trams (or find them in the loaded mapping) and set their data. Call after the NewGRFs are loaded. */
void MetroSetupTramEngines()
{
	for (uint i = 0; i < std::size(_metro_trams); i++) {
		const MetroTramSpec &spec = _metro_trams[i];
		uint16_t local_id = static_cast<uint16_t>(i);
		EngineID id = _engine_mngr.GetID(VehicleType::Road, local_id, METRO_TRAM_GRFID);
		Engine *e;
		if (id == EngineID::Invalid()) {
			if (!Engine::CanAllocateItem()) return;
			/* A local ID out of the original range: no vanilla data is copied. */
			e = Engine::Create(VehicleType::Road, static_cast<uint16_t>(GetOriginalEngineCount(VehicleType::Road) + i));
			assert(_engine_mngr.mappings.size() == e->index.base());
			_engine_mngr.mappings.push_back({METRO_TRAM_GRFID, local_id, VehicleType::Road, GetOriginalEngineCount(VehicleType::Road)});
			_engine_mngr.AddToIndex(e->index);
			if (_gted.size() < Engine::GetPoolSize()) _gted.resize(Engine::GetPoolSize());
		} else {
			e = Engine::Get(id);
		}

		EngineInfo &ei = e->info;
		ei.base_intro = CalTime::ConvertYMDToDate(CalTime::Year{spec.intro_year}, 0, 1);
		ei.lifelength = CalTime::YearDelta{METRO_TRAM_VEHICLE_LIFE};
		ei.base_life = CalTime::YearDelta{METRO_TRAM_MODEL_LIFE};
		ei.decay_speed = METRO_TRAM_DECAY_SPEED;
		ei.load_amount = 5;
		ei.climates = LandscapeTypes({LandscapeType::Temperate, LandscapeType::Arctic, LandscapeType::Tropic, LandscapeType::Toyland});
		ei.cargo_type = INVALID_CARGO;
		ei.cargo_label = CT_PASSENGERS;
		ei.refit_mask = {};
		ei.refit_cost = 8;
		ei.misc_flags = EngineMiscFlags{EngineMiscFlag::RoadIsTram};
		ei.callback_mask = {};
		ei.retire_early = 0;
		ei.extra_flags = {};
		ei.string_id = spec.name;
		ei.cargo_age_period = CARGO_AGING_TICKS;
		ei.variant_id = EngineID::Invalid();

		RoadVehicleInfo &rvi = e->VehInfo<RoadVehicleInfo>();
		rvi.image_index = spec.image_index;
		rvi.cost_factor = spec.cost_factor;
		rvi.running_cost = spec.running_cost;
		rvi.running_cost_class = Price::RunningRoadveh;
		rvi.sfx = spec.sound;
		rvi.max_speed = spec.speed_kmh * 2;
		rvi.capacity = spec.capacity;
		rvi.weight = spec.weight_t * 4;
		rvi.power = spec.power_10hp;
		rvi.tractive_effort = 76;
		rvi.air_drag = 0;
		rvi.visual_effect = VE_DEFAULT;
		rvi.shorten_factor = 0;
		rvi.roadtype = ROADTYPE_TRAM;
		e->original_image_index = spec.image_index;
	}
}

/** Trams that the loaded game did not have yet start like in a new game. */
void MetroTramsAfterLoad()
{
	for (Engine *e : Engine::IterateType(VehicleType::Road)) {
		if (IsMetroTramEngineScope(_engine_mngr.mappings[e->index].grfid) && e->intro_date == CalTime::Date{0}) StartupEngineAddedAfterLoad(e);
	}
}

bool ConMetroTrams(std::span<std::string_view> argv)
{
	if (argv.size() > 2 && argv[1] == "track") {
		/* For tests: lay dedicated tram track along the town road through the centre (X axis); compare with the plain tramway. */
		auto id = ParseInteger<uint>(argv[2]);
		const Town *t = id.has_value() ? Town::GetIfValid(*id) : nullptr;
		if (t == nullptr) return false;
		auto is_road_x = [](TileIndex tile) { return IsValidTile(tile) && IsTileType(tile, TileType::Road) && IsNormalRoadTile(tile) && HasTileRoadType(tile, RoadTramType::Road) && (GetRoadBits(tile, RoadTramType::Road) & ROAD_X) == ROAD_X; };
		if (!is_road_x(t->xy)) {
			IConsolePrint(CC_ERROR, "No road along X at the centre.");
			return true;
		}
		TileIndex a = t->xy, b = t->xy;
		while (is_road_x(TileAddByDiagDir(a, DiagDirection::NE))) a = TileAddByDiagDir(a, DiagDirection::NE);
		while (is_road_x(TileAddByDiagDir(b, DiagDirection::SW))) b = TileAddByDiagDir(b, DiagDirection::SW);
		CompanyID company = CompanyID::Invalid();
		for (const Company *c : Company::Iterate()) {
			if (company == CompanyID::Invalid()) company = c->index;
		}
		Backup<CompanyID> cur_company(_current_company, company, FILE_LINE);
		DoCommandFlags test{DoCommandFlag::Bankrupt};
		CommandCost plain = Command<Commands::BuildRoadLong>::Do(test, b, a, ROADTYPE_TRAM, Axis::X, DisallowedRoadDirections{}, false, false, false);
		CommandCost dedicated = Command<Commands::BuildRoadLong>::Do(DoCommandFlags{DoCommandFlag::Execute, DoCommandFlag::Bankrupt}, b, a, ROADTYPE_METRO_TRAM, Axis::X, DisallowedRoadDirections{}, false, false, false);
		cur_company.Restore();
		IConsolePrint(CC_INFO, "road ({}, {})..({}, {}), {} tiles: plain tramway {} ({}), dedicated {} ({}); centre tram type {}", TileX(a), TileY(a), TileX(b), TileY(b), DistanceManhattan(a, b) + 1,
				plain.Succeeded() ? "ok" : GetString(plain.GetErrorMessage()), plain.GetCost(), dedicated.Succeeded() ? "built" : GetString(dedicated.GetErrorMessage()), dedicated.GetCost(),
				GetRoadTypeTram(t->xy) == ROADTYPE_METRO_TRAM ? "dedicated" : "other");
		return true;
	}
	if (argv.size() < 2 || argv[1] != "list") {
		IConsolePrint(CC_HELP, "Metro mod trams. Usage: metro_trams list | track <town id>");
		return true;
	}
	CompanyID company = Company::IsValidID(_local_company) ? _local_company : CompanyID::Invalid();
	for (const Company *c : Company::Iterate()) {
		if (company == CompanyID::Invalid()) company = c->index;
	}
	for (const Engine *e : Engine::IterateType(VehicleType::Road)) {
		const EngineIDMapping &eid = _engine_mngr.mappings[e->index];
		bool tram = e->info.misc_flags.Test(EngineMiscFlag::RoadIsTram);
		if (!tram && !IsMetroTramEngineScope(eid.grfid)) continue;
		IConsolePrint(CC_DEFAULT, "engine {} '{}': built-in {}, tram {}, intro {}, speed {} km/h, capacity {}, cost {}, buildable {}", e->index, GetString(STR_ENGINE_NAME, e->index),
				IsMetroTramEngineScope(eid.grfid) ? "yes" : "no", tram ? "yes" : "no", e->intro_date, e->GetDisplayMaxSpeed(), e->GetDisplayDefaultCapacity(), e->GetCost(),
				company != CompanyID::Invalid() && IsEngineBuildable(e->index, VehicleType::Road, company) ? "yes" : "no");
	}
	return true;
}
