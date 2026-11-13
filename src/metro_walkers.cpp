/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_walkers.cpp Metro mod: pedestrians drawn by code (stage 10).
 *
 * The pixels come from the templates of table/metro_walker_tables.h: head, torso and legs by style and body, an
 * umbrella on top if needed, mirrored for SW and NW. Colours are palette indices (nearest to the RGB of the table),
 * the clothes use the company colour ramp so the palette of each pedestrian recolours them. The sprites are
 * palette sprites, so they work with every blitter. metro_sprites.cpp registers them after the own PNG sprites
 * and asks #MetroWalkerDraw for the pixels when the sprite cache needs them.
 */

#include "stdafx.h"
#include "metro_walkers.h"
#include "date_func.h"
#include "direction_type.h"
#include "gfx_func.h"
#include "landscape.h"
#include "settings_type.h"
#include "sprite.h"
#include "tile_map.h"
#include "core/format.hpp"
#include "table/metro_walker_tables.h"

#ifdef WITH_PNG
#include <png.h>
#endif /* WITH_PNG */

#include <filesystem>
#include <unordered_map>

#include "safeguards.h"

int _metro_walker_force_style = -1;
int _metro_walker_force_umbrella = -1;

static std::unordered_map<uint32_t, uint8_t> _walker_palette_cache; ///< RGB -> nearest palette index.

void MetroWalkersResetPalette()
{
	_walker_palette_cache.clear();
}

/** Nearest palette index to an RGB colour (not transparent, not the company ramp, not animated). */
static uint8_t NearestPaletteIndex(uint32_t rgb)
{
	auto it = _walker_palette_cache.find(rgb);
	if (it != _walker_palette_cache.end()) return it->second;
	int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
	uint8_t best = 1;
	int best_dist = INT32_MAX;
	for (uint i = 1; i < 0xD7; i++) {
		if (i >= 0xC6 && i <= 0xCD) continue; // Company colour ramp.
		const Colour &c = _cur_palette.palette[i];
		int dist = (c.r - r) * (c.r - r) * 3 + (c.g - g) * (c.g - g) * 4 + (c.b - b) * (c.b - b) * 2;
		if (dist < best_dist) {
			best_dist = dist;
			best = static_cast<uint8_t>(i);
		}
	}
	_walker_palette_cache[rgb] = best;
	return best;
}

static uint32_t Darken(uint32_t rgb, uint percent)
{
	uint r = ((rgb >> 16) & 0xFF) * percent / 100, g = ((rgb >> 8) & 0xFF) * percent / 100, b = (rgb & 0xFF) * percent / 100;
	return (r << 16) | (g << 8) | b;
}

/**
 * Pixels of a pedestrian sprite at 1x as palette indices (0 = transparent).
 * @param index Sprite index: ((((style * 2 + umbrella) * 2 + body) * 2 + skin) * 4 + direction) * 4 + frame.
 * @return False for an unknown index.
 */
bool MetroWalkerDraw(uint index, std::vector<uint8_t> &pixels, uint &width, uint &height)
{
	if (index >= METRO_WALKER_SPRITE_COUNT) return false;
	uint frame = index % METRO_WALKER_FRAMES; index /= METRO_WALKER_FRAMES;
	DiagDirection dir = static_cast<DiagDirection>(index % METRO_WALKER_DIRS); index /= METRO_WALKER_DIRS;
	uint skin = index % METRO_WALKER_SKINS; index /= METRO_WALKER_SKINS;
	uint body = index % METRO_WALKER_BODIES; index /= METRO_WALKER_BODIES;
	bool umbrella = (index % 2) != 0; index /= 2;
	uint style = index;
	const MetroWalkerLook &look = _metro_walker_looks[style][body];
	bool back = dir == DiagDirection::NE || dir == DiagDirection::NW;
	bool mirror = dir == DiagDirection::SW || dir == DiagDirection::NW;

	const uint top = umbrella ? METRO_WALKER_UMBRELLA_ROWS : 0;
	width = METRO_WALKER_WIDTH;
	height = METRO_WALKER_HEIGHT + top;
	std::vector<char> grid(width * height, '.');
	auto put_row = [&](uint y, const char *row) {
		for (uint x = 0; x < width && row[x] != '\0'; x++) grid[y * width + x] = row[x];
	};
	const char * const *head = back ? look.head.back : look.head.front;
	const char * const *torso = frame == 0 ? look.torso.swing0 : (frame == 2 ? look.torso.swing2 : look.torso.hang);
	for (uint r = 0; r < 3; r++) {
		put_row(top + r, head[r]);
		put_row(top + 3 + r, torso[r]);
		put_row(top + 6 + r, look.legs.frame[frame][r]);
	}
	if (umbrella) {
		for (uint r = 0; r < METRO_WALKER_UMBRELLA_ROWS; r++) put_row(r, _metro_walker_umbrella[r]);
		const uint hx = METRO_WALKER_UMBRELLA_HANDLE_X;
		/* Handle from the canopy past the head to the raised hand at the shoulder. */
		for (uint y = top; y < top + 3; y++) {
			if (grid[y * width + hx] == '.') grid[y * width + hx] = 'l';
		}
		grid[(top + 3) * width + hx] = 's';
		/* The holding arm no longer hangs. */
		for (uint y = top + 4; y < top + 6; y++) {
			for (uint x = hx; x < width; x++) {
				char &c = grid[y * width + x];
				if (c == 's' || c == 'd' || c == 'S') c = '.';
			}
		}
	}
	if (mirror) {
		for (uint y = 0; y < height; y++) std::reverse(grid.begin() + y * width, grid.begin() + (y + 1) * width);
	}

	const MetroWalkerSkin &sk = _metro_walker_skins[skin];
	auto colour = [&](char c) -> uint8_t {
		switch (c) {
			case 'c': return METRO_WALKER_RAMP_MAIN;
			case 'C': return METRO_WALKER_RAMP_LIGHT;
			case 'd': return METRO_WALKER_RAMP_DARK;
			case 's': return NearestPaletteIndex(sk.skin);
			case 'S': return NearestPaletteIndex(sk.shade);
			case 'h': return NearestPaletteIndex(sk.hair);
			case 'H': return NearestPaletteIndex(METRO_WALKER_HAT);
			case 'k': return NearestPaletteIndex(METRO_WALKER_HAT_DARK);
			case 'p': return NearestPaletteIndex(look.trousers);
			case 'q': return NearestPaletteIndex(look.trousers_dark);
			case 'f': return NearestPaletteIndex(METRO_WALKER_SHOES);
			case 'w': return NearestPaletteIndex(METRO_WALKER_WHITE);
			case 'g': return NearestPaletteIndex(METRO_WALKER_SILVER);
			case 'G': return NearestPaletteIndex(METRO_WALKER_SILVER_DARK);
			case 'x': return NearestPaletteIndex(METRO_WALKER_GLOW);
			case 'o': return NearestPaletteIndex(METRO_WALKER_COAT[body]);
			case 'O': return NearestPaletteIndex(METRO_WALKER_COAT_DARK[body]);
			case 'u': return NearestPaletteIndex(look.umbrella);
			case 'v': return NearestPaletteIndex(Darken(look.umbrella, METRO_WALKER_UMBRELLA_EDGE_DARKEN));
			case 'l': return NearestPaletteIndex(METRO_WALKER_UMBRELLA_HANDLE);
			default: return 0;
		}
	};
	pixels.resize(grid.size());
	for (size_t i = 0; i < grid.size(); i++) pixels[i] = colour(grid[i]);
	return true;
}

/** Look of a pedestrian by the date, the place and its number. */
MetroWalkerChoice MetroWalkerChoose(TileIndex tile, uint32_t id, uint8_t colour)
{
	int year = CalTime::CurYear().base();
	uint month = CalTime::CurMonth();
	MetroWalkerStyle style = MetroWalkerStyle::Fifties;
	if (year >= METRO_WALKER_YEAR_FUTURE) {
		style = MetroWalkerStyle::Future;
	} else if (year >= METRO_WALKER_YEAR_NOUGHTIES) {
		style = MetroWalkerStyle::Noughties;
	} else if (year >= METRO_WALKER_YEAR_SEVENTIES) {
		style = MetroWalkerStyle::Seventies;
	}
	LandscapeType climate = _settings_game.game_creation.landscape;
	bool winter = (climate == LandscapeType::Arctic && GetTileZ(tile) > GetSnowLine()) ||
			(climate == LandscapeType::Temperate && (month == 11 || month <= 1));
	if (winter) style = MetroWalkerStyle::Winter;
	bool rainy = !winter && ((climate == LandscapeType::Temperate && (month == 2 || month == 3 || month == 9 || month == 10)) ||
			(climate == LandscapeType::Tropic && GetTropicZone(tile) == TropicZone::Rainforest));

	/* Mix the number so neighbours differ in body, skin and umbrella. */
	uint32_t h = id * 2654435761u;
	bool umbrella = rainy && (h >> 8) % METRO_WALKER_UMBRELLA_ONE_IN == 0;
	if (_metro_walker_force_style >= 0) style = static_cast<MetroWalkerStyle>(std::min<int>(_metro_walker_force_style, METRO_WALKER_STYLES - 1));
	if (_metro_walker_force_umbrella >= 0) umbrella = _metro_walker_force_umbrella != 0;
	uint body = (h >> 16) % METRO_WALKER_BODIES;
	uint skin = (h >> 20) % METRO_WALKER_SKINS;

	MetroWalkerChoice choice;
	choice.style = to_underlying(style);
	choice.umbrella = umbrella;
	choice.index = (((choice.style * 2 + (umbrella ? 1 : 0)) * METRO_WALKER_BODIES + body) * METRO_WALKER_SKINS + skin) * METRO_WALKER_DIRS * METRO_WALKER_FRAMES;
	choice.pal = GetColourPalette(_metro_walker_clothes[choice.style][colour % std::size(_metro_walker_clothes[0])]);
	return choice;
}

/**
 * Write the pedestrians into PNG sheets for viewing (console "metro_sprites walkers <folder>"): one file per style,
 * rows = umbrella x body x skin, columns = direction x frame, 4 times larger, company ramp shown as is.
 * @return Number of files written.
 */
uint MetroWalkersDump(const std::string &dir)
{
	uint written = 0;
#ifdef WITH_PNG
	static const char * const names[] = { "fifties", "seventies", "noughties", "future", "winter" };
	constexpr uint SCALE = 4;
	constexpr uint CELL_W = (METRO_WALKER_WIDTH + 2) * SCALE;
	constexpr uint CELL_H = (METRO_WALKER_HEIGHT + METRO_WALKER_UMBRELLA_ROWS + 2) * SCALE;
	constexpr uint COLS = METRO_WALKER_DIRS * METRO_WALKER_FRAMES;
	constexpr uint ROWS = 2 * METRO_WALKER_BODIES * METRO_WALKER_SKINS;
	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::u8path(dir), ec);
	for (uint style = 0; style < METRO_WALKER_STYLES; style++) {
		std::vector<uint8_t> rgba(CELL_W * COLS * CELL_H * ROWS * 4);
		for (size_t i = 0; i < rgba.size(); i += 4) {
			rgba[i] = 120; rgba[i + 1] = 170; rgba[i + 2] = 110; rgba[i + 3] = 255; // Grass-like background.
		}
		for (uint row = 0; row < ROWS; row++) {
			for (uint col = 0; col < COLS; col++) {
				uint index = (style * ROWS + row) * COLS + col;
				std::vector<uint8_t> px;
				uint w, h;
				if (!MetroWalkerDraw(index, px, w, h)) continue;
				uint ox = col * CELL_W + SCALE, oy = row * CELL_H + CELL_H - SCALE - h * SCALE;
				for (uint y = 0; y < h * SCALE; y++) {
					for (uint x = 0; x < w * SCALE; x++) {
						uint8_t m = px[(y / SCALE) * w + x / SCALE];
						if (m == 0) continue;
						const Colour &c = _cur_palette.palette[m];
						size_t o = ((oy + y) * CELL_W * COLS + ox + x) * 4;
						rgba[o] = c.r; rgba[o + 1] = c.g; rgba[o + 2] = c.b;
					}
				}
			}
		}
		png_image image{};
		image.version = PNG_IMAGE_VERSION;
		image.width = CELL_W * COLS;
		image.height = CELL_H * ROWS;
		image.format = PNG_FORMAT_RGBA;
		std::string file = (std::filesystem::u8path(dir) / fmt::format("walkers_{}.png", names[style])).string();
		if (png_image_write_to_file(&image, file.c_str(), 0, rgba.data(), 0, nullptr)) written++;
	}
#endif /* WITH_PNG */
	return written;
}
