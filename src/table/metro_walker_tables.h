/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_walker_tables.h Metro mod: pedestrians drawn by code (stage 10), pixel templates and colours.
 *
 * A pedestrian is 6 x 9 pixels at 1x (12 with an umbrella), drawn facing the viewer (SE, walking down-right on the
 * screen) and from the back (NE, up-right); SW and NW are mirrors. Rows 0-2 are the head (row 0: hat top), 3-5
 * the torso with the arms, 6-8 the legs. Letters are colours (#MetroWalkerPixel):
 *   .  transparent
 *   c C d   clothes: company colour ramp (main, light, dark) -> recoloured per pedestrian
 *   s S     skin (main, shade)                h  hair
 *   H k     hat (main, dark)                  p q  trousers / skirt (main, dark)
 *   f       shoes                             w  white (sneakers, boots, hair of the future)
 *   g G     silver (suit of the future)       x  glow (visor and belt of the future)
 *   o O     winter coat (main, dark)          u v l  umbrella (canopy, edge, handle)
 */

#ifndef METRO_WALKER_TABLES_H
#define METRO_WALKER_TABLES_H

#include "../gfx_type.h"

static constexpr uint METRO_WALKER_WIDTH = 6;
static constexpr uint METRO_WALKER_HEIGHT = 9;
static constexpr uint METRO_WALKER_UMBRELLA_ROWS = 3; ///< Rows of the canopy above the head.

/** Clothes of the pedestrians. */
enum class MetroWalkerStyle : uint8_t {
	Fifties,   ///< Before 1965: hat and coat / dress.
	Seventies, ///< 1965-1989: long hair, flares / mini dress.
	Noughties, ///< 1990-2029: jeans and sneakers.
	Future,    ///< From 2030: silver suit with a glowing visor.
	Winter,    ///< In the snow or in winter months: knitted hat, scarf, coat.
	End,
};

static constexpr uint METRO_WALKER_STYLES = to_underlying(MetroWalkerStyle::End);
static constexpr uint METRO_WALKER_BODIES = 2; ///< 0: trousers, 1: skirt / dress.
static constexpr uint METRO_WALKER_SKINS = 2;
static constexpr uint METRO_WALKER_DIRS = 4;   ///< NE, SE, SW, NW (order of #DiagDirection).
static constexpr uint METRO_WALKER_FRAMES = 4; ///< Steps of the walk.
/** All sprites: style x umbrella x body x skin x direction x frame. */
static constexpr uint METRO_WALKER_SPRITE_COUNT = METRO_WALKER_STYLES * 2 * METRO_WALKER_BODIES * METRO_WALKER_SKINS * METRO_WALKER_DIRS * METRO_WALKER_FRAMES;

/** Years when the clothes change (as the town cars, stage 6). */
static constexpr int METRO_WALKER_YEAR_SEVENTIES = 1965;
static constexpr int METRO_WALKER_YEAR_NOUGHTIES = 1990;
static constexpr int METRO_WALKER_YEAR_FUTURE = 2030;
/** One in this many pedestrians carries an umbrella in the rainy months (temperate: March, April, October, November; sub-tropic: in the rainforest). */
static constexpr uint METRO_WALKER_UMBRELLA_ONE_IN = 3;

/** A head: 3 rows, facing the viewer and from the back. */
struct MetroWalkerHead {
	const char *front[3];
	const char *back[3];
};

/** A torso: 3 rows; arms swinging (frames 0 and 2) and hanging (frames 1 and 3). */
struct MetroWalkerTorso {
	const char *swing0[3];
	const char *hang[3];
	const char *swing2[3];
};

/** Legs: 3 rows for each frame. */
struct MetroWalkerLegs {
	const char *frame[4][3];
};

struct MetroWalkerLook {
	MetroWalkerHead head;
	MetroWalkerTorso torso;
	MetroWalkerLegs legs;
	uint32_t trousers;      ///< RGB of p.
	uint32_t trousers_dark; ///< RGB of q.
	uint32_t umbrella;      ///< RGB of u.
};

/* Common parts. */
#define MW_HEAD_SHORT { { "......", "..hh..", "..ss.." }, { "......", "..hh..", "..hh.." } }
#define MW_TORSO(top, mid, low_l, low_n, low_r) \
	{ { top, "sd" mid "d.", low_l }, { top, ".d" mid "d.", low_n }, { top, ".d" mid "ds", low_r } }
#define MW_TROUSERS(p, q, f) { { \
	{ ".." p q "..", "." p ".." q ".", "." f ".." f "." }, \
	{ ".." p q "..", ".." p q "..", ".." f f ".." }, \
	{ ".." p q "..", "." q ".." p ".", "." f ".." f "." }, \
	{ ".." p q "..", ".." p q "..", ".." f f ".." } } }
#define MW_SKIRT(hem, f) { { \
	{ hem, ".s..s.", ".f..f." }, \
	{ hem, "..ss..", ".." f f ".." }, \
	{ hem, ".S..S.", ".f..f." }, \
	{ hem, "..ss..", ".." f f ".." } } }

/** Looks by style and body. */
static const MetroWalkerLook _metro_walker_looks[METRO_WALKER_STYLES][METRO_WALKER_BODIES] = {
	{ /* Fifties */
		{ /* Hat and a long coat over dark trousers. */
			{ { "..kk..", ".kHHk.", "..ss.." }, { "..kk..", ".kHHk.", "..hh.." } },
			MW_TORSO(".dCcd.", "Cc", "..cc.s", ".scds.", ".sdc.."),
			{ { { ".dccd.", ".q..q.", ".f..f." }, { ".dccd.", "..qq..", "..ff.." }, { ".dccd.", ".q..q.", ".f..f." }, { ".dccd.", "..qq..", "..ff.." } } },
			0x5A5A64, 0x3C3C46, 0x2C2C34,
		},
		{ /* Bun of hair and a dress. */
			{ { "...h..", "..hh..", "..ss.." }, { "...h..", "..hh..", "..hh.." } },
			MW_TORSO(".dCcd.", "Cc", "..cc.s", ".sccs.", ".scc.."),
			MW_SKIRT(".cCcc.", "f"),
			0x5A5A64, 0x3C3C46, 0xB42828,
		},
	},
	{ /* Seventies */
		{ /* Long hair and flares. */
			{ { "......", ".hhhh.", ".hssh." }, { "......", ".hhhh.", ".hhhh." } },
			MW_TORSO(".hCch.", "Cc", "..pp.s", ".spps.", ".spp.."),
			{ { { "..pq..", ".p..q.", "pp..qq" }, { "..pq..", "..pq..", ".pqqp." }, { "..pq..", ".q..p.", "qq..pp" }, { "..pq..", "..pq..", ".pqqp." } } },
			0x96643C, 0x6E4628, 0x2C2C34,
		},
		{ /* Long hair, mini dress and white boots. */
			{ { "......", ".hhhh.", ".hssh." }, { "......", ".hhhh.", ".hhhh." } },
			MW_TORSO(".hCch.", "Cc", "..cc.s", ".sccs.", ".scc.."),
			{ { { ".cCcc.", ".s..s.", ".w..w." }, { ".cCcc.", "..ss..", "..ww.." }, { ".cCcc.", ".S..S.", ".w..w." }, { ".cCcc.", "..ss..", "..ww.." } } },
			0x96643C, 0x6E4628, 0xE6A01E,
		},
	},
	{ /* Noughties */
		{ /* Short hair, T-shirt, jeans and white sneakers. */
			MW_HEAD_SHORT,
			MW_TORSO(".sCcs.", "Cc", "..pp.s", ".spps.", ".spp.."),
			MW_TROUSERS("p", "q", "w"),
			0x4664AA, 0x2E4278, 0x2C2C34,
		},
		{ /* Ponytail, top, dark leggings and sneakers. */
			{ { "......", "..hhh.", "..ssh." }, { "......", "..hhh.", "..hhh." } },
			MW_TORSO(".dCcd.", "Cc", "..pp.s", ".spps.", ".spp.."),
			MW_TROUSERS("p", "q", "w"),
			0x32323C, 0x1E1E24, 0x3C8CDC,
		},
	},
	{ /* Future */
		{ /* Silver suit, glowing visor and belt, white boots. */
			{ { "......", "..ww..", "..xx.." }, { "......", "..ww..", "..ww.." } },
			MW_TORSO(".cggc.", "gG", "..xx.s", ".sxxs.", ".sxx.."),
			MW_TROUSERS("g", "G", "w"),
			0, 0, 0xDCDCE6,
		},
		{ /* Silver dress with a glowing hem. */
			{ { "......", ".wwww.", ".wxxw." }, { "......", ".wwww.", ".wwww." } },
			MW_TORSO(".cggc.", "gG", "..gg.s", ".sggs.", ".sgg.."),
			MW_SKIRT(".xxxx.", "w"),
			0, 0, 0xDCDCE6,
		},
	},
	{ /* Winter */
		{ /* Knitted hat with a pompon, scarf, dark coat, boots. */
			{ { "...C..", "..cc..", "..ss.." }, { "...C..", "..cc..", "..hh.." } },
			MW_TORSO(".OccO.", "oo", "..oo.s", ".soos.", ".soo.."),
			{ { { ".oooo.", ".q..q.", ".f..f." }, { ".oooo.", "..qq..", "..ff.." }, { ".oooo.", ".q..q.", ".f..f." }, { ".oooo.", "..qq..", "..ff.." } } },
			0x3C3C46, 0x28282E, 0x2C2C34,
		},
		{ /* Same in a red-brown coat. */
			{ { "...C..", "..cc..", "..ss.." }, { "...C..", "..cc..", "..hh.." } },
			MW_TORSO(".OccO.", "oo", "..oo.s", ".soos.", ".soo.."),
			{ { { ".oooo.", ".q..q.", ".f..f." }, { ".oooo.", "..qq..", "..ff.." }, { ".oooo.", ".q..q.", ".f..f." }, { ".oooo.", "..qq..", "..ff.." } } },
			0x3C3C46, 0x28282E, 0xB42828,
		},
	},
};

#undef MW_HEAD_SHORT
#undef MW_TORSO
#undef MW_TROUSERS
#undef MW_SKIRT

/** Canopy of the umbrella above the head (the handle goes down at column 4 to the raised hand). */
static const char * const _metro_walker_umbrella[METRO_WALKER_UMBRELLA_ROWS] = { "..uu..", ".uuuu.", "uvuvuv" };
static constexpr uint METRO_WALKER_UMBRELLA_HANDLE_X = 4;

/** Fixed colours (RGB), found in the palette at load time. */
struct MetroWalkerSkin {
	uint32_t skin, shade, hair;
};
static const MetroWalkerSkin _metro_walker_skins[METRO_WALKER_SKINS] = {
	{ 0xE8B894, 0xC48C6C, 0x60401E },
	{ 0x8C6040, 0x684428, 0x28201C },
};
static constexpr uint32_t METRO_WALKER_HAT = 0x584838;
static constexpr uint32_t METRO_WALKER_HAT_DARK = 0x382C24;
static constexpr uint32_t METRO_WALKER_SHOES = 0x28201C;
static constexpr uint32_t METRO_WALKER_WHITE = 0xECECEC;
static constexpr uint32_t METRO_WALKER_SILVER = 0xC8CCD4;
static constexpr uint32_t METRO_WALKER_SILVER_DARK = 0x9498A4;
static constexpr uint32_t METRO_WALKER_GLOW = 0x50E6F0;
static constexpr uint32_t METRO_WALKER_COAT[METRO_WALKER_BODIES] = { 0x3C4060, 0x78322A };
static constexpr uint32_t METRO_WALKER_COAT_DARK[METRO_WALKER_BODIES] = { 0x282A3C, 0x54221C };
static constexpr uint32_t METRO_WALKER_UMBRELLA_EDGE_DARKEN = 60; ///< Percent of the canopy colour on its edge.
static constexpr uint32_t METRO_WALKER_UMBRELLA_HANDLE = 0x28201C;

/** Company colour ramp (recoloured by the palette of the pedestrian): main, light, dark. */
static constexpr uint8_t METRO_WALKER_RAMP_MAIN = 0xC9;
static constexpr uint8_t METRO_WALKER_RAMP_LIGHT = 0xCB;
static constexpr uint8_t METRO_WALKER_RAMP_DARK = 0xC7;

/** Colours of the clothes by style (the pedestrian's colour number picks one). */
static const Colours _metro_walker_clothes[METRO_WALKER_STYLES][8] = {
	{ Colours::Grey, Colours::Brown, Colours::DarkBlue, Colours::DarkGreen, Colours::Cream, Colours::White, Colours::Grey, Colours::Brown },
	{ Colours::Orange, Colours::Yellow, Colours::Red, Colours::Purple, Colours::Mauve, Colours::Brown, Colours::Green, Colours::Pink },
	{ Colours::Red, Colours::Blue, Colours::LightBlue, Colours::Green, Colours::Pink, Colours::White, Colours::Grey, Colours::Orange },
	{ Colours::White, Colours::LightBlue, Colours::PaleGreen, Colours::Grey, Colours::Cream, Colours::White, Colours::LightBlue, Colours::Mauve },
	{ Colours::Red, Colours::DarkBlue, Colours::Green, Colours::Yellow, Colours::Red, Colours::Orange, Colours::Purple, Colours::White },
};

#endif /* METRO_WALKER_TABLES_H */
