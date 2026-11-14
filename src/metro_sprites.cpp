/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro_sprites.cpp Metro mod: own sprites from PNG files and sprites drawn by code (stage 10).
 *
 * The converter (mod-sprites/metro_convert_sprites.py) writes 32bpp PNG files for the zooms 1x, 2x and 4x into
 * mod-sprites/game/<section>/. After the base set and the NewGRFs are loaded, every sprite of
 * #_metro_sprite_specs whose files exist gets sprite numbers after the last loaded sprite; the sprite cache reads
 * them from the PNG files (#MetroLoadCustomSprite). The drawing code asks #MetroSprite for a sprite with the
 * vanilla sprite it used before as the fallback, so a missing file changes nothing. PNG sprites are used only with
 * a 32bpp blitter. Company colours are not remapped yet (no mask channel).
 *
 * After them come the sprites drawn by code (pedestrians, metro_walkers.cpp): palette sprites that work with every
 * blitter, generated when the sprite cache needs them.
 */

#include "stdafx.h"
#include "metro_sprites.h"
#include "metro_walkers.h"
#include "metro_stations.h"
#include "cargotype.h"
#include "house.h"
#include "town.h"
#include "town_map.h"
#include "tile_cmd.h"
#include "transparency.h"
#include "viewport_func.h"
#include "table/metro_station_tables.h"
#include "core/string_consumer.hpp"
#include "blitter/factory.hpp"
#include "console_func.h"
#include "console_type.h"
#include "debug.h"
#include "engine_base.h"
#include "engine_override.h"
#include "fileio_func.h"
#include "object_base.h"
#include "object_type.h"
#include "spritecache.h"
#include "spritecache_internal.h"
#include "spriteloader/spriteloader.hpp"
#include "zoom_func.h"
#include "table/strings.h"
#include "table/metro_sprite_tables.h"
#include "table/metro_walker_tables.h"
#include "table/metro_train_tables.h"
#include "table/metro_tram_tables.h"
#include "table/sprites.h"

#ifdef WITH_PNG
#include <png.h>
#endif /* WITH_PNG */

#include <filesystem>

#include "safeguards.h"

/** One image (one direction, part or frame) of an own sprite: its files by zoom. */
struct MetroSpriteImage {
	std::array<std::string, 3> files; ///< 1x, 2x, 4x; empty if missing.
	MetroSpriteAnchor anchor;
	int walker = -1;                   ///< Pedestrian drawn by code (index for #MetroWalkerDraw), or -1 for a PNG sprite.
	bool present = true;               ///< False for a view of a vehicle that has no file (the vanilla one is drawn).
};

static std::vector<MetroSpriteImage> _metro_sprite_images;       ///< Image of each own sprite number from #_metro_sprite_first.
static SpriteID _metro_sprite_first = 0;
static std::array<SpriteID, to_underlying(MetroSpriteId::End)> _metro_sprite_base{}; ///< First sprite of each own sprite, 0 = not loaded.
static std::string _metro_sprite_dir;                               ///< Folder the sprites were found in.
static SpriteID _metro_walker_base = 0;                             ///< First pedestrian drawn by code, 0 = none.

/** UTF-8 text of a path. */
static std::string PathString(const std::filesystem::path &p)
{
	std::u8string s = p.u8string();
	return std::string(s.begin(), s.end());
}

/** Number of images of a kind. */
static uint ImageCount(MetroSpriteKind kind)
{
	switch (kind) {
		case MetroSpriteKind::Single: return 1;
		case MetroSpriteKind::Parts4: return 4;
		case MetroSpriteKind::Dir8: return 8;
		case MetroSpriteKind::Diag4x4: return 16;
	}
	return 1;
}

/** File name of an image without the zoom suffix. */
static std::string ImageName(const MetroSpriteSpec &spec, uint image)
{
	switch (spec.kind) {
		case MetroSpriteKind::Single: return spec.name;
		case MetroSpriteKind::Parts4: return fmt::format("{}_{}", spec.name, image);
		case MetroSpriteKind::Dir8: return fmt::format("{}_{}", spec.name, _metro_sprite_dir8[image]);
		case MetroSpriteKind::Diag4x4: return fmt::format("{}_{}_{}", spec.name, _metro_sprite_diag4[image / 4], image % 4);
	}
	return spec.name;
}

/** The folder with the converted sprites, or empty. */
static std::string FindSpriteDir()
{
	std::vector<std::filesystem::path> bases;
	for (Searchpath sp : _valid_searchpaths) bases.emplace_back(std::filesystem::u8path(FioGetDirectory(sp, Subdirectory::Base)));
	bases.emplace_back(std::filesystem::current_path());
	for (const std::filesystem::path &base : bases) {
		for (const char *dir : _metro_sprite_dirs) {
			std::error_code ec;
			std::filesystem::path p = (base / std::filesystem::u8path(dir)).lexically_normal();
			if (std::filesystem::is_directory(p, ec)) return PathString(p);
		}
	}
	return {};
}

/** Register the own sprites whose files exist (after the base set and NewGRFs are loaded). */
void MetroSpritesLoad()
{
	_metro_sprite_images.clear();
	_metro_sprite_base.fill(0);
	_metro_sprite_first = GetMaxSpriteID();
	_metro_sprite_dir.clear();
	_metro_walker_base = 0;
	MetroWalkersResetPalette();
	if (BlitterFactory::GetCurrentBlitter() == nullptr) return;
#ifdef WITH_PNG
	if (BlitterFactory::GetCurrentBlitter()->GetScreenDepth() == 32) _metro_sprite_dir = FindSpriteDir();
	std::filesystem::path root = std::filesystem::u8path(_metro_sprite_dir);
	for (uint i = 0; i < std::size(_metro_sprite_specs) && !_metro_sprite_dir.empty(); i++) {
		const MetroSpriteSpec &spec = _metro_sprite_specs[i];
		std::vector<MetroSpriteImage> images;
		bool any = false;
		for (uint image = 0; image < ImageCount(spec.kind); image++) {
			MetroSpriteImage &img = images.emplace_back();
			img.anchor = spec.anchor;
			static const uint zooms[] = { 1, 2, 4 };
			for (uint z = 0; z < 3; z++) {
				std::error_code ec;
				std::filesystem::path file = root / spec.section / (ImageName(spec, image) + fmt::format("_{}x.png", zooms[z]));
				if (std::filesystem::is_regular_file(file, ec)) {
					img.files[z] = PathString(file);
					any = true;
				}
			}
		}
		/* All images of the sprite need at least one zoom, otherwise the vanilla sprite stays; a vehicle may have
		 * only some of its views (the others stay vanilla). */
		for (MetroSpriteImage &img : images) img.present = std::ranges::any_of(img.files, [](const std::string &f) { return !f.empty(); });
		bool complete = std::ranges::all_of(images, [](const MetroSpriteImage &img) { return img.present; });
		if (!any) continue;
		if (!complete && spec.kind != MetroSpriteKind::Dir8) {
			Debug(sprite, 0, "[metro_sprites] {}/{}: some images are missing, the vanilla sprite stays", spec.section, spec.name);
			continue;
		}
		_metro_sprite_base[i] = _metro_sprite_first + static_cast<SpriteID>(_metro_sprite_images.size());
		for (MetroSpriteImage &img : images) _metro_sprite_images.push_back(std::move(img));
	}
#endif /* WITH_PNG */

	/* Pedestrians drawn by code: with any blitter. */
	_metro_walker_base = _metro_sprite_first + static_cast<SpriteID>(_metro_sprite_images.size());
	for (uint i = 0; i < METRO_WALKER_SPRITE_COUNT; i++) {
		MetroSpriteImage &img = _metro_sprite_images.emplace_back();
		img.anchor = MetroSpriteAnchor::Walker;
		img.walker = static_cast<int>(i);
	}

	/* Slots in the sprite cache; the data is read from the files when drawn. */
	SpriteFile *file = GetOriginFile(SPR_IMG_QUERY);
	for (uint i = 0; i < _metro_sprite_images.size(); i++) {
		SpriteCache *sc = AllocateSpriteCache(_metro_sprite_first + i);
		sc->file = file;
		sc->file_pos = 0;
		sc->id = 0;
		sc->count = 1;
		sc->SetType(SpriteType::Normal);
		sc->flags = 0;
		SB(sc->flags, SCC_32BPP_ZOOM_START, 6, 0x3F);
		sc->total_missing_zoom_levels = {};
	}
	Debug(sprite, 1, "[metro_sprites] {} own sprites from '{}', {} pedestrians drawn by code", _metro_sprite_images.size() - METRO_WALKER_SPRITE_COUNT, _metro_sprite_dir, METRO_WALKER_SPRITE_COUNT);
}

/** Whether the sprite number is an own sprite. */
bool MetroIsCustomSprite(SpriteID id)
{
	return !_metro_sprite_images.empty() && id >= _metro_sprite_first && id < _metro_sprite_first + _metro_sprite_images.size();
}

#ifdef WITH_PNG
/** Read one PNG file as RGBA; false if it can't be read. */
static bool ReadPng(const std::string &filename, std::vector<uint8_t> &rgba, uint &width, uint &height)
{
	auto f = FileHandle::Open(filename, "rb");
	if (!f.has_value()) return false;
	png_image image{};
	image.version = PNG_IMAGE_VERSION;
	if (!png_image_begin_read_from_stdio(&image, *f)) return false;
	image.format = PNG_FORMAT_RGBA;
	rgba.resize(PNG_IMAGE_SIZE(image));
	if (!png_image_finish_read(&image, nullptr, rgba.data(), 0, nullptr)) {
		png_image_free(&image);
		return false;
	}
	width = image.width;
	height = image.height;
	return width > 0 && height > 0 && width <= UINT16_MAX && height <= UINT16_MAX;
}
#endif /* WITH_PNG */

/** Offsets of an image of @p width x @p height at zoom @p scale (1, 2, 4). */
static void AnchorOffsets(MetroSpriteAnchor anchor, int width, int height, int scale, int16_t &x_offs, int16_t &y_offs)
{
	switch (anchor) {
		case MetroSpriteAnchor::Building: x_offs = -width / 2; y_offs = METRO_SPRITE_TILE_BOTTOM * scale - height; break;
		case MetroSpriteAnchor::Building2x2: x_offs = -width / 2; y_offs = METRO_SPRITE_2X2_BOTTOM * scale - height; break;
		case MetroSpriteAnchor::Ground: x_offs = -(width / 2) + scale; y_offs = 0; break;
		case MetroSpriteAnchor::Vehicle: x_offs = -width / 2; y_offs = -(height * 3 / 4); break;
		case MetroSpriteAnchor::Walker: x_offs = -width / 2; y_offs = -height; break;
		case MetroSpriteAnchor::Icon: x_offs = 0; y_offs = 0; break;
	}
}

/**
 * Fill the zooms of an own sprite from its PNG files (for the sprite cache).
 * @return The zooms loaded.
 */
LowZoomLevels MetroLoadCustomSprite(SpriteID id, SpriteLoader::SpriteCollection &sprite)
{
	LowZoomLevels loaded{};
	if (!MetroIsCustomSprite(id)) return loaded;
	const MetroSpriteImage &img = _metro_sprite_images[id - _metro_sprite_first];
	static const ZoomLevel zooms[] = { ZoomLevel::Normal, ZoomLevel::In2x, ZoomLevel::In4x };
	static const int scales[] = { 1, 2, 4 };

	if (img.walker >= 0) {
		/* Drawn by code at 1x as palette indices; 2x and 4x by repeating the pixels. */
		std::vector<uint8_t> px;
		uint w, h;
		if (!MetroWalkerDraw(static_cast<uint>(img.walker), px, w, h)) return loaded;
		for (uint z = 0; z < 3; z++) {
			const uint scale = scales[z];
			SpriteLoader::Sprite &s = sprite[zooms[z]];
			s.width = static_cast<uint16_t>(w * scale);
			s.height = static_cast<uint16_t>(h * scale);
			AnchorOffsets(img.anchor, s.width, s.height, scale, s.x_offs, s.y_offs);
			s.colours = {SpriteComponent::Palette};
			s.AllocateData(zooms[z], static_cast<size_t>(s.width) * s.height);
			for (uint y = 0; y < s.height; y++) {
				for (uint x = 0; x < s.width; x++) {
					SpriteLoader::CommonPixel &p = s.data[static_cast<size_t>(y) * s.width + x];
					uint8_t m = px[(y / scale) * w + x / scale];
					p.r = p.g = p.b = 0;
					p.m = m;
					p.a = m == 0 ? 0 : 0xFF;
				}
			}
			loaded.Set(zooms[z]);
		}
		return loaded;
	}
#ifdef WITH_PNG
	for (uint z = 0; z < 3; z++) {
		if (img.files[z].empty()) continue;
		std::vector<uint8_t> rgba;
		uint w, h;
		if (!ReadPng(img.files[z], rgba, w, h)) {
			Debug(sprite, 0, "[metro_sprites] can't read {}", img.files[z]);
			continue;
		}
		SpriteLoader::Sprite &s = sprite[zooms[z]];
		s.width = static_cast<uint16_t>(w);
		s.height = static_cast<uint16_t>(h);
		AnchorOffsets(img.anchor, w, h, scales[z], s.x_offs, s.y_offs);
		s.colours = {SpriteComponent::RGB, SpriteComponent::Alpha};
		s.AllocateData(zooms[z], static_cast<size_t>(w) * h);
		for (size_t i = 0; i < static_cast<size_t>(w) * h; i++) {
			s.data[i].r = rgba[i * 4];
			s.data[i].g = rgba[i * 4 + 1];
			s.data[i].b = rgba[i * 4 + 2];
			s.data[i].a = rgba[i * 4 + 3];
			s.data[i].m = 0;
		}
		loaded.Set(zooms[z]);
	}
#endif /* WITH_PNG */
	return loaded;
}

/** Whether an image of a loaded own sprite has a file. */
static bool HasImage(SpriteID base, uint index)
{
	return base != 0 && _metro_sprite_images[base - _metro_sprite_first + index].present;
}

SpriteID MetroSprite(MetroSpriteId id, SpriteID fallback, uint index)
{
	SpriteID base = _metro_sprite_base[to_underlying(id)];
	return HasImage(base, index) ? base + index : fallback;
}

bool MetroHasSprite(MetroSpriteId id)
{
	return _metro_sprite_base[to_underlying(id)] != 0;
}

SpriteID MetroWalkerSprite(uint index)
{
	return _metro_walker_base == 0 || index >= METRO_WALKER_SPRITE_COUNT ? 0 : _metro_walker_base + index;
}

/** An own view of a sprite in the direction; for symmetric rolling stock also the view of the opposite direction. */
static bool VehicleView(MetroSpriteId id, uint dir, bool symmetric, SpriteID &sprite)
{
	SpriteID base = _metro_sprite_base[to_underlying(id)];
	if (HasImage(base, dir)) {
		sprite = base + dir;
		return true;
	}
	uint opposite = (dir + 4) & 7;
	if (symmetric && HasImage(base, opposite)) {
		sprite = base + opposite;
		return true;
	}
	return false;
}

/**
 * Own sprite of a metro train or tram in the direction; false to keep the vanilla one.
 * Metro cars: a freight car carrying coal, mail or other freight uses the wagon of its cargo; a passenger car
 * without its own picture uses the head car of its series. Metro rolling stock looks the same both ways, so a view
 * drawn for one direction also serves the opposite one; trams and cars use only their own views.
 */
bool MetroSpritesVehicleImage(EngineID engine, Direction direction, SpriteID &sprite, CargoType cargo)
{
	if (_metro_sprite_images.empty() || engine.base() >= _engine_mngr.mappings.size()) return false;
	const EngineIDMapping &eid = _engine_mngr.mappings[engine];
	uint dir = to_underlying(direction) & 7;
	if (eid.grfid == METRO_TRAIN_GRFID && eid.internal_id < 2 * std::size(_metro_trains)) {
		MetroSpriteId id = static_cast<MetroSpriteId>(to_underlying(MetroSpriteId::TrainFirst) + eid.internal_id);
		bool is_car = (eid.internal_id % 2) != 0;
		if (is_car && IsValidCargoType(cargo) && !IsCargoInClass(cargo, CargoClass::Passengers)) {
			MetroSpriteId wagon = MetroSpriteId::WagonCovered;
			if (IsCargoInClass(cargo, CargoClass::Mail)) {
				wagon = MetroSpriteId::WagonMail;
			} else if (CargoSpec::Get(cargo)->label == CT_COAL) {
				wagon = MetroSpriteId::WagonCoal;
			}
			if (VehicleView(wagon, dir, true, sprite)) return true;
		}
		if (VehicleView(id, dir, true, sprite)) return true;
		if (is_car) return VehicleView(static_cast<MetroSpriteId>(to_underlying(id) - 1), dir, true, sprite);
		return false;
	}
	if (eid.grfid == METRO_TRAM_GRFID && eid.internal_id < std::size(_metro_trams)) {
		return VehicleView(static_cast<MetroSpriteId>(to_underlying(MetroSpriteId::TramFirst) + eid.internal_id), dir, false, sprite);
	}
	return false;
}

/**
 * Draw a vanilla landmark house with an own sprite: stadiums (one picture over their 2x2 area, drawn from the north
 * tile) and parks. The ground is already drawn.
 * @return False to draw the vanilla house.
 */
bool MetroSpritesDrawHouse(const TileInfo *ti, HouseID house)
{
	if (_metro_sprite_images.empty() || house >= NEW_HOUSE_OFFSET || !IsHouseCompleted(ti->tile)) return false;
	StringID name = HouseSpec::Get(house)->building_name;
	if (name == STR_TOWN_BUILDING_NAME_PARK_1) {
		if (!MetroHasSprite(MetroSpriteId::Park)) return false;
		AddSortableSpriteToDraw(MetroSprite(MetroSpriteId::Park, 0), PAL_NONE, *ti, SpriteBounds({0, 0, 0}, {TILE_SIZE, TILE_SIZE, 16}, {}), IsTransparencySet(TransparencyOption::Houses));
		return true;
	}
	if (name == STR_TOWN_BUILDING_NAME_STADIUM_1 || name == STR_TOWN_BUILDING_NAME_STADIUM_2) {
		if (!MetroHasSprite(MetroSpriteId::Stadium)) return false;
		HouseID north = house;
		if (GetHouseNorthPart(north) != 0) return true; // The other tiles show only their ground.
		AddSortableSpriteToDraw(MetroSprite(MetroSpriteId::Stadium, 0), PAL_NONE, *ti, SpriteBounds({0, 0, 0}, {2 * TILE_SIZE, 2 * TILE_SIZE, 24}, {}), IsTransparencySet(TransparencyOption::Houses));
		return true;
	}
	return false;
}

/** Own sprite of a tile of a mod object (services, power grid, metro entrance), or 0 to draw the vanilla one. */
SpriteID MetroSpritesObjectImage(TileIndex tile)
{
	if (_metro_sprite_images.empty()) return 0;
	const Object *o = Object::GetByTile(tile);
	MetroSpriteId id;
	switch (o->type) {
		case OBJECT_METRO_SCHOOL: id = MetroSpriteId::School; break;
		case OBJECT_METRO_HOSPITAL: id = MetroSpriteId::Hospital; break;
		case OBJECT_METRO_UNIVERSITY: id = MetroSpriteId::University; break;
		case OBJECT_METRO_SUBSTATION: id = MetroSpriteId::Substation; break;
		case OBJECT_METRO_GAS_PLANT: id = MetroSpriteId::GasPlant; break;
		case OBJECT_METRO_NUCLEAR_PLANT: id = MetroSpriteId::NuclearPlant; break;
		case OBJECT_METRO_SOLAR_PLANT: id = MetroSpriteId::SolarPlant; break;
		case OBJECT_METRO_WIND_PLANT: id = MetroSpriteId::WindTurbine; break;
		case OBJECT_METRO_VENT_SHAFT: id = MetroSpriteId::VentShaft; break;
		case OBJECT_METRO_ENTRANCE:
			/* Interchange and megahub stations get the big pavilion. */
			id = MetroHasSprite(MetroSpriteId::EntranceBig) && MetroEntranceStationLevel(tile) >= METRO_STATION_LEVEL_INTERCHANGE ? MetroSpriteId::EntranceBig : MetroSpriteId::Entrance;
			break;
		default: return 0;
	}
	if (!MetroHasSprite(id)) return 0;
	uint part = 0;
	if (_metro_sprite_specs[to_underlying(id)].kind == MetroSpriteKind::Parts4) {
		uint dx = std::min<uint>(TileX(tile) - TileX(o->location.tile), 1);
		uint dy = std::min<uint>(TileY(tile) - TileY(o->location.tile), 1);
		part = dy * 2 + dx;
	}
	return MetroSprite(id, 0, part);
}

/** Console "metro_sprites": which own sprites are loaded. */
bool ConMetroSprites(std::span<std::string_view> argv)
{
	if (argv.size() >= 3 && argv[1] == "walkers") {
		/* Pedestrians drawn by code into PNG sheets for viewing. */
		IConsolePrint(CC_INFO, "walkers: {} sheet(s) written to '{}'", MetroWalkersDump(std::string(argv[2])), argv[2]);
		return true;
	}
	if (argv.size() >= 3 && argv[1] == "walkerstyle") {
		/* Force the clothes of all pedestrians (tests): walkerstyle <0-4|-1> [umbrella 0|1|-1]. */
		_metro_walker_force_style = ParseInteger<int>(argv[2]).value_or(-1);
		_metro_walker_force_umbrella = argv.size() >= 4 ? ParseInteger<int>(argv[3]).value_or(-1) : -1;
		IConsolePrint(CC_INFO, "walker style forced to {}, umbrella {}", _metro_walker_force_style, _metro_walker_force_umbrella);
		return true;
	}
	uint walkers = _metro_walker_base != 0 ? METRO_WALKER_SPRITE_COUNT : 0;
	IConsolePrint(CC_INFO, "own sprites: {} PNG images from '{}', first sprite {}; pedestrians drawn by code: {} from sprite {}",
			_metro_sprite_images.size() - walkers, _metro_sprite_dir, _metro_sprite_first, walkers, _metro_walker_base);
	for (uint i = 0; i < std::size(_metro_sprite_specs); i++) {
		const MetroSpriteSpec &spec = _metro_sprite_specs[i];
		std::string state = "vanilla";
		if (_metro_sprite_base[i] != 0) {
			uint have = 0, total = ImageCount(spec.kind);
			for (uint k = 0; k < total; k++) have += HasImage(_metro_sprite_base[i], k) ? 1 : 0;
			state = have == total ? fmt::format("sprite {}", _metro_sprite_base[i]) : fmt::format("sprite {}, {} of {} views (others vanilla)", _metro_sprite_base[i], have, total);
		}
		IConsolePrint(_metro_sprite_base[i] != 0 ? CC_INFO : CC_DEFAULT, "  {}/{}: {}", spec.section, spec.name, state);
	}
	return true;
}
