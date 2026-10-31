/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file rail_type.h The different types of rail. */

#ifndef RAIL_TYPE_H
#define RAIL_TYPE_H

#include "core/enum_type.hpp"
#include "core/label_type.hpp"

using RailTypeLabel = Label<struct RailTypeLabelTag>;

static const RailTypeLabel RAILTYPE_LABEL_RAIL{"RAIL"};
static const RailTypeLabel RAILTYPE_LABEL_ELECTRIC{"ELRL"};
static const RailTypeLabel RAILTYPE_LABEL_MONO{"MONO"};
static const RailTypeLabel RAILTYPE_LABEL_MAGLEV{"MGLV"};
static const RailTypeLabel RAILTYPE_LABEL_METRO_1{"MTR1"}; ///< Metro mod.
static const RailTypeLabel RAILTYPE_LABEL_METRO_2{"MTR2"}; ///< Metro mod.
static const RailTypeLabel RAILTYPE_LABEL_METRO_3{"MTR3"}; ///< Metro mod.
static const RailTypeLabel RAILTYPE_LABEL_METRO_X{"MTRX"}; ///< Metro mod.

/**
 * Enumeration for all possible railtypes.
 */
enum RailType : uint8_t {
	RAILTYPE_BEGIN    = 0,          ///< Used for iterations
	RAILTYPE_RAIL     = 0,          ///< Standard non-electric rails
	RAILTYPE_ELECTRIC = 1,          ///< Electric rails
	RAILTYPE_MONO     = 2,          ///< Monorail
	RAILTYPE_MAGLEV   = 3,          ///< Maglev
	RAILTYPE_METRO_1  = 4,          ///< Metro mod: metro track of layer -1
	RAILTYPE_METRO_2  = 5,          ///< Metro mod: metro track of layer -2
	RAILTYPE_METRO_3  = 6,          ///< Metro mod: metro track of layer -3
	RAILTYPE_METRO_X  = 7,          ///< Metro mod: metro track where layers cross or meet (inclines): every metro train runs on it
	RAILTYPE_END      = 64,         ///< Used for iterations
	INVALID_RAILTYPE  = 0xFF,       ///< Flag for invalid railtype
};

/** Bitset of \c RailType elements. */
using RailTypes = EnumBitSet<RailType, uint64_t>;

template <>
struct BaseBitSetEnableDirectIteration<RailTypes> {
	static const bool value = true;
};

static constexpr RailTypes INVALID_RAILTYPES{UINT64_MAX};

#endif /* RAIL_TYPE_H */
