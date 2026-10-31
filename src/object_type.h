/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file object_type.h Types related to object tiles. */

#ifndef OBJECT_TYPE_H
#define OBJECT_TYPE_H

#include "core/pool_id_type.hpp"

/** Types of objects. */
typedef uint16_t ObjectType;

static const ObjectType OBJECT_TRANSMITTER  =   0;    ///< The large antenna
static const ObjectType OBJECT_LIGHTHOUSE   =   1;    ///< The nice lighthouse
static const ObjectType OBJECT_STATUE       =   2;    ///< Statue in towns
static const ObjectType OBJECT_OWNED_LAND   =   3;    ///< Owned land 'flag'
static const ObjectType OBJECT_HQ           =   4;    ///< HeadQuarter of a player
static const ObjectType OBJECT_METRO_SCHOOL       = 5; ///< Metro mod: school of a town
static const ObjectType OBJECT_METRO_HOSPITAL     = 6; ///< Metro mod: hospital of a town
static const ObjectType OBJECT_METRO_UNIVERSITY   = 7; ///< Metro mod: university of a town
static const ObjectType OBJECT_METRO_RETIRED      = 8; ///< Metro mod: reserved, was the fire station (metro_city v2..v3); such objects are removed on load
static const ObjectType METRO_OBJECT_COUNT        = 4; ///< Metro mod: number of reserved object types (NewGRF objects of older saves are moved up by this)
static const ObjectType OBJECT_METRO_SUBSTATION    =  9; ///< Metro mod: power substation
static const ObjectType OBJECT_METRO_GAS_PLANT     = 10; ///< Metro mod: gas power plant
static const ObjectType OBJECT_METRO_NUCLEAR_PLANT = 11; ///< Metro mod: nuclear power plant
static const ObjectType OBJECT_METRO_SOLAR_PLANT   = 12; ///< Metro mod: solar power plant
static const ObjectType OBJECT_METRO_WIND_PLANT    = 13; ///< Metro mod: wind farm
static const ObjectType OBJECT_METRO_VENT_SHAFT    = 14; ///< Metro mod: vent shaft of the deep metro
static const ObjectType METRO_POWER_OBJECT_COUNT   =  6; ///< Metro mod: object types of the power grid (NewGRF objects of saves before metro_power move up by this)
static const ObjectType OBJECT_METRO_ENTRANCE      = 15; ///< Metro mod: entrance pavilion of the metro
static const ObjectType METRO_UNDERGROUND_OBJECT_COUNT = 1; ///< Metro mod: object types of the underground (NewGRF objects of saves before metro_underground move up by this)

static const ObjectType NEW_OBJECT_OFFSET   =  16;    ///< Offset for new objects
static const ObjectType NUM_OBJECTS         = 64000;  ///< Number of supported objects overall
static const ObjectType NUM_OBJECTS_PER_GRF = NUM_OBJECTS; ///< Number of supported objects per NewGRF
static const ObjectType INVALID_OBJECT_TYPE = 0xFFFF; ///< An invalid object

/** Unique identifier for an object. */
struct ObjectIDTag : public PoolIDTraits<uint32_t, 0xFF0000, 0xFFFFFFFF> {};
using ObjectID = PoolID<ObjectIDTag>;

struct Object;
struct ObjectSpec;

/** Animation triggers for objects. */
enum class ObjectAnimationTrigger : uint8_t {
	Built, ///< Triggered when the object is built (for all tiles at the same time).
	TileLoop, ///< Triggered in the periodic tile loop.
	TileLoopNorth, ///< Triggered every 256 ticks (for all tiles at the same time).
};

/** Bitset of \c ObjectAnimationTrigger elements. */
using ObjectAnimationTriggers = EnumBitSet<ObjectAnimationTrigger, uint16_t>;

enum ObjectEffectiveFoundationType {
	OEFT_NONE,
	OEFT_FLAT,
	OEFT_INCLINE_X,
	OEFT_INCLINE_Y,
};

#endif /* OBJECT_TYPE_H */
