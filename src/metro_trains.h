/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_trains.h Metro mod: metro trains and metro track types (stage 8, step 6). */

#ifndef METRO_TRAINS_H
#define METRO_TRAINS_H

#include "engine_type.h"
#include "newgrf_type.h"
#include "rail_type.h"
#include "company_type.h"

/** Whether the rail type is metro track. */
inline bool IsMetroRailType(RailType rt)
{
	return rt >= RAILTYPE_METRO_1 && rt <= RAILTYPE_METRO_X;
}

/**
 * Whether a depot of rail type @p depot does not sell an engine of @p engine: surface trains may drive into the metro
 * through the transformer, but metro depots sell only metro trains.
 */
inline bool MetroDepotRefusesEngine(RailTypes engine, RailType depot)
{
	return IsMetroRailType(depot) && !engine.Any({RAILTYPE_METRO_1, RAILTYPE_METRO_2, RAILTYPE_METRO_3});
}

void MetroSetupTrainEngines();
void MetroFinaliseTrainRefits();
void MetroTrainsAfterLoad();
bool IsMetroTrainEngineScope(GrfID grfid);
EngineID MetroFindTrainEngine(CompanyID company, uint layer, bool car);
void MetroListTrainEngines(CompanyID company);

#endif /* METRO_TRAINS_H */
