/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro_dispatcher_tables.h Metro mod: numbers of the dispatcher (stage 9, decisions of the user). */

#ifndef METRO_DISPATCHER_TABLES_H
#define METRO_DISPATCHER_TABLES_H

static constexpr int METRO_DISPATCH_SPEED_STEP = 10;     ///< W / S change the target speed by this many km-ish/h.
static constexpr uint METRO_DISPATCH_RED_FINE_PIECES = 10; ///< Running a red signal costs this many rail pieces (with inflation).
static constexpr uint METRO_DISPATCH_SIGNAL_LOOKAHEAD = 16; ///< Tiles searched for the next signal on the panel.

#endif /* METRO_DISPATCHER_TABLES_H */
