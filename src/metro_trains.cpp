/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_trains.cpp Metro mod: the metro trains (stage 8, step 6).
 *
 * Each series of #_metro_trains is two engines in the scope #METRO_TRAIN_GRFID of the engine ID mapping (as if a
 * NewGRF defined them): local ID 2*i the head, 2*i+1 the car. Their rail types are the metro track types of their
 * layers (#RAILTYPE_METRO_1..3), so a train runs only on its layers and on the shared #RAILTYPE_METRO_X (inclines,
 * crossings of layers). Their data come from the table after every NewGRF load.
 */

#include "stdafx.h"
#include "metro_trains.h"
#include "metro_underground.h"
#include "engine_base.h"
#include "engine_func.h"
#include "engine_override.h"
#include "cargotype.h"
#include "date_func.h"
#include "rail.h"
#include "company_base.h"
#include "console_func.h"
#include "console_type.h"
#include "strings_func.h"
#include "newgrf/newgrf_internal_vehicle.h"
#include "table/strings.h"
#include "table/metro_train_tables.h"

#include "safeguards.h"

bool IsMetroTrainEngineScope(GrfID grfid)
{
	return grfid == METRO_TRAIN_GRFID;
}

/** Rail types of the layers in @p layers (ML*). */
static RailTypes MetroLayersRailTypes(uint8_t layers)
{
	RailTypes rts{};
	for (uint l = 1; l <= 3; l++) {
		if (HasBit(layers, l - 1)) rts.Set(GetMetroLayerRailType(l));
	}
	return rts;
}

/** Find or create the engine with the local ID in the metro scope. */
static Engine *GetOrCreateMetroEngine(uint16_t local_id)
{
	EngineID id = _engine_mngr.GetID(VehicleType::Train, local_id, METRO_TRAIN_GRFID);
	if (id != EngineID::Invalid()) return Engine::Get(id);
	if (!Engine::CanAllocateItem()) return nullptr;
	/* A local ID out of the original range: no vanilla data is copied. */
	Engine *e = Engine::Create(VehicleType::Train, static_cast<uint16_t>(GetOriginalEngineCount(VehicleType::Train) + local_id));
	assert(_engine_mngr.mappings.size() == e->index.base());
	_engine_mngr.mappings.push_back({METRO_TRAIN_GRFID, local_id, VehicleType::Train, GetOriginalEngineCount(VehicleType::Train)});
	_engine_mngr.AddToIndex(e->index);
	if (_gted.size() < Engine::GetPoolSize()) _gted.resize(Engine::GetPoolSize());
	return e;
}

/** The rail types of a metro engine; FinaliseEngineArray takes them from the labels (or disables the engine). */
static void SetMetroEngineRailTypes(Engine *e, RailTypes rts)
{
	RailVehicleInfo &rvi = e->VehInfo<RailVehicleInfo>();
	rvi.railtypes = rts;
	rvi.intended_railtypes = rts;
	auto &labels = _gted[e->index].railtypelabels;
	labels.clear();
	for (RailType rt : rts) labels.push_back(GetRailTypeInfo(rt)->label);
}

/** Engine data shared by heads and cars. */
static void SetMetroEngineInfo(Engine *e, CalTime::Date intro, StringID name)
{
	EngineInfo &ei = e->info;
	ei.base_intro = intro;
	ei.lifelength = CalTime::YearDelta{METRO_TRAIN_VEHICLE_LIFE};
	ei.base_life = CalTime::YearDelta{METRO_TRAIN_MODEL_LIFE};
	ei.decay_speed = METRO_TRAIN_DECAY_SPEED;
	ei.load_amount = METRO_TRAIN_LOAD_AMOUNT;
	ei.climates = LandscapeTypes({LandscapeType::Temperate, LandscapeType::Arctic, LandscapeType::Tropic, LandscapeType::Toyland});
	ei.cargo_type = INVALID_CARGO;
	ei.cargo_label = CT_PASSENGERS;
	ei.refit_mask = {};
	ei.refit_cost = 8;
	ei.misc_flags = {};
	ei.callback_mask = {};
	ei.retire_early = 0;
	ei.extra_flags = {};
	ei.string_id = name;
	ei.cargo_age_period = CARGO_AGING_TICKS;
	ei.variant_id = EngineID::Invalid();
}

/** Create the metro trains (or find them in the loaded mapping) and set their data. Call after the NewGRFs are loaded. */
void MetroSetupTrainEngines()
{
	for (uint i = 0; i < std::size(_metro_trains); i++) {
		const MetroTrainSpec &spec = _metro_trains[i];
		int32_t year = spec.intro_year != 0 ? spec.intro_year : GetMetroLayerYear(spec.intro_layer);
		CalTime::Date intro = CalTime::ConvertYMDToDate(CalTime::Year{year}, 0, 1);
		RailTypes rts = MetroLayersRailTypes(spec.layers);

		Engine *head = GetOrCreateMetroEngine(static_cast<uint16_t>(2 * i));
		if (head == nullptr) return;
		SetMetroEngineInfo(head, intro, spec.name);
		head->info.misc_flags = EngineMiscFlags{EngineMiscFlag::RailIsMU};
		RailVehicleInfo &rvi = head->VehInfo<RailVehicleInfo>();
		rvi = RailVehicleInfo{};
		rvi.image_index = spec.image_index;
		rvi.railveh_type = spec.single ? RailVehicleType::Singlehead : RailVehicleType::Multihead;
		rvi.cost_factor = spec.cost_factor;
		rvi.max_speed = spec.speed_kmh;
		rvi.power = spec.power_hp;
		rvi.weight = spec.weight_t;
		rvi.running_cost = spec.running_cost;
		rvi.running_cost_class = Price::RunningTrainElectric;
		rvi.engclass = EngineClass::Electric;
		rvi.capacity = spec.capacity;
		rvi.visual_effect = VE_DEFAULT;
		rvi.tractive_effort = 76;
		head->original_image_index = spec.image_index;
		SetMetroEngineRailTypes(head, rts);

		Engine *car = GetOrCreateMetroEngine(static_cast<uint16_t>(2 * i + 1));
		if (car == nullptr) return;
		SetMetroEngineInfo(car, intro, spec.car_name);
		RailVehicleInfo &cvi = car->VehInfo<RailVehicleInfo>();
		cvi = RailVehicleInfo{};
		cvi.image_index = spec.image_index;
		cvi.railveh_type = RailVehicleType::Wagon;
		cvi.cost_factor = spec.car_cost;
		cvi.weight = spec.car_weight_t;
		cvi.running_cost_class = Price::Invalid;
		cvi.engclass = EngineClass::Steam;
		cvi.capacity = spec.capacity;
		cvi.visual_effect = VE_DEFAULT;
		cvi.tractive_effort = 76;
		car->original_image_index = spec.image_index;
		SetMetroEngineRailTypes(car, rts);
	}
}

/** The cars carry any cargo: after the default refit masks are computed. */
void MetroFinaliseTrainRefits()
{
	for (Engine *e : Engine::IterateType(VehicleType::Train)) {
		const EngineIDMapping &eid = _engine_mngr.mappings[e->index];
		if (!IsMetroTrainEngineScope(eid.grfid) || (eid.internal_id % 2) == 0) continue;
		e->info.refit_mask = _cargo_mask;
		if (!IsValidCargoType(e->info.cargo_type) && _cargo_mask.Any()) {
			for (CargoType c : _cargo_mask) {
				e->info.cargo_type = c;
				break;
			}
		}
	}
}

/** Metro trains that the loaded game did not have yet start like in a new game. */
void MetroTrainsAfterLoad()
{
	for (Engine *e : Engine::IterateType(VehicleType::Train)) {
		if (IsMetroTrainEngineScope(_engine_mngr.mappings[e->index].grfid) && e->intro_date == CalTime::Date{0}) StartupEngineAddedAfterLoad(e);
	}
}

/** The newest buildable metro head (or car) for the layer, for tests; EngineID::Invalid() if none. */
EngineID MetroFindTrainEngine(CompanyID company, uint layer, bool car)
{
	EngineID best = EngineID::Invalid();
	RailType rt = GetMetroLayerRailType(layer);
	for (const Engine *e : Engine::IterateType(VehicleType::Train)) {
		const EngineIDMapping &eid = _engine_mngr.mappings[e->index];
		if (!IsMetroTrainEngineScope(eid.grfid) || ((eid.internal_id % 2) == 1) != car) continue;
		if (!e->VehInfo<RailVehicleInfo>().railtypes.Test(rt) || !IsEngineBuildable(e->index, VehicleType::Train, company)) continue;
		if (best == EngineID::Invalid() || Engine::Get(best)->intro_date < e->intro_date) best = e->index;
	}
	return best;
}

/** Console: the metro trains with their availability for the company. */
void MetroListTrainEngines(CompanyID company)
{
	for (const Engine *e : Engine::IterateType(VehicleType::Train)) {
		if (!IsMetroTrainEngineScope(_engine_mngr.mappings[e->index].grfid)) continue;
		const RailVehicleInfo &rvi = e->VehInfo<RailVehicleInfo>();
		bool valid = company != CompanyID::Invalid();
		IConsolePrint(CC_INFO, "engine {} '{}': intro {}, {} km/h, capacity {}, rail types 0x{:X}, available {}, company {}, enabled {}, buildable {}", e->index, GetString(e->info.string_id),
				e->intro_date, rvi.max_speed, rvi.capacity, rvi.railtypes.base(), e->flags.Test(EngineFlag::Available), valid && e->company_avail.Test(company), e->IsEnabled(),
				valid && IsEngineBuildable(e->index, VehicleType::Train, company));
	}
	if (const Company *c = Company::GetIfValid(company); c != nullptr) IConsolePrint(CC_INFO, "company rail types 0x{:X}", c->avail_railtypes.base());
}
