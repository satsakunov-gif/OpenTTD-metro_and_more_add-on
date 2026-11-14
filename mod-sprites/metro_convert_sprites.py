#!/usr/bin/env python3
"""
Metro mod: sprite converter (stage 10).

Takes the source pictures from mod-sprites/raw/<section>/<name>.<png|jpg|jpeg|webp|bmp>, cleans them and writes
32bpp PNG files for the zooms 1x, 2x and 4x into mod-sprites/game/<section>/<name>_<zoom>x.png. The game loads
them at start; a sprite without files keeps its vanilla look.

Steps for every picture:
  1. Background: transparent pixels, or a white / magenta (#FF00FF) background found from the borders of the
     picture (flood fill from the edges, so white inside the object stays).
  2. Soft shadows: grey pixels "darker than the background" joined to it below the middle of the object, and
     half-transparent dark pixels of transparent pictures.
  3. Small specks (JPEG noise) are dropped, the alpha becomes hard (0 or 255), empty margins are trimmed.
  4. Nearest-neighbour scaling to the size of the object in the game (mod-sprites/metro_sprites.csv), separately
     for every zoom; then a dark outline of 1 px around the picture (not for ground tiles).

Vehicles: when only 4 views (N, NE, E, SE) are drawn, the other ones are mirrored (SW from SE, W from E, NW from
NE, S from N). Pedestrians: NW from NE and SW from SE.

Usage (from the root of the repository):
    python mod-sprites/metro_convert_sprites.py                     convert everything found in raw/
    python mod-sprites/metro_convert_sprites.py --only train_a_head  only the sprites whose name contains this
    python mod-sprites/metro_convert_sprites.py --list              the files expected in raw/
    python mod-sprites/metro_convert_sprites.py --inbox             what is waiting in inbox/
    python mod-sprites/metro_convert_sprites.py --assign inbox/x.jpg metro/train_a_head_SE
                                                                    copy an inbox picture into raw/ under its name
    python mod-sprites/metro_convert_sprites.py --clean             delete game/ first
    python mod-sprites/metro_convert_sprites.py --debug             also write the cleaned full-size pictures
                                                                    into game/_debug/ (to tune the options)
Options to tune the cleaning: --bg-tolerance, --shadow-tolerance, --no-shadow, --outline, --outline-colour,
--filter. See --help.

Needs Python 3.8+, Pillow, numpy and scipy (pip install pillow numpy scipy).
"""

import argparse
import csv
import shutil
import sys
from pathlib import Path

try:
    import numpy as np
    from PIL import Image
    from scipy import ndimage
except ImportError:
    sys.exit("Pillow, numpy and scipy are needed: pip install pillow numpy scipy")

ROOT = Path(__file__).resolve().parent
RAW = ROOT / "raw"
GAME = ROOT / "game"
INBOX = ROOT / "inbox"
TABLE = ROOT / "metro_sprites.csv"
ZOOMS = (1, 2, 4)
EXTENSIONS = (".png", ".jpg", ".jpeg", ".webp", ".bmp")

DIR8 = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]
DIAG4 = ["NE", "SE", "SW", "NW"]
FRAMES = 4
# A missing view is the mirror of this one (horizontal mirror on the screen).
MIRROR = {"S": "N", "N": "S", "SW": "SE", "SE": "SW", "W": "E", "E": "W", "NW": "NE", "NE": "NW"}


# ------------------------------------------------------------------------------------------------
# Table of sprites
# ------------------------------------------------------------------------------------------------

def load_specs():
    with open(TABLE, encoding="utf-8") as f:
        return list(csv.DictReader(f))


def image_names(spec):
    """Names (without extension) of all images of a sprite, in the order the game expects."""
    name, kind = spec["name"], spec["kind"]
    if kind == "single":
        return [name]
    if kind == "parts4":
        return [f"{name}_{p}" for p in range(4)]
    if kind == "dir8":
        return [f"{name}_{d}" for d in DIR8]
    if kind == "diag4x4":
        return [f"{name}_{d}_{f}" for d in DIAG4 for f in range(FRAMES)]
    raise ValueError(f"unknown kind {kind}")


def mirror_source(spec, image_name):
    """For a view that may be mirrored: the name of its mirror partner, else None."""
    kind = spec["kind"]
    if kind not in ("dir8", "diag4x4"):
        return None
    rest = image_name[len(spec["name"]) + 1:]
    parts = rest.split("_")
    partner = MIRROR.get(parts[0])
    if partner is None or (kind == "diag4x4" and partner not in DIAG4):
        return None
    return "_".join([spec["name"], partner] + parts[1:])


def find_source(folder, name):
    """The source file of an image in any supported format, or None."""
    for ext in EXTENSIONS:
        p = folder / f"{name}{ext}"
        if p.exists():
            return p
    return None


# ------------------------------------------------------------------------------------------------
# Cleaning
# ------------------------------------------------------------------------------------------------

def luminance(rgb):
    return rgb[..., 0] * 0.299 + rgb[..., 1] * 0.587 + rgb[..., 2] * 0.114


def detect_background(rgb, alpha):
    """Kind of background from the border pixels: 'alpha', 'white', 'magenta' or a colour tuple."""
    border = np.concatenate([alpha[0, :], alpha[-1, :], alpha[:, 0], alpha[:, -1]])
    if np.mean(border < 128) > 0.5:
        return "alpha", None
    pixels = np.concatenate([rgb[0, :], rgb[-1, :], rgb[:, 0], rgb[:, -1]]).astype(np.int32)
    colour = np.median(pixels, axis=0)
    r, g, b = colour
    if r > 200 and b > 200 and g < 80:
        return "magenta", np.array([255, 0, 255])
    if min(r, g, b) > 200:
        return "white", colour
    return "colour", colour


def clean(img, opts, name="", debug_dir=None):
    """Picture -> RGBA numpy array with the background and soft shadows removed, trimmed; None if empty."""
    rgba = np.array(img.convert("RGBA"), dtype=np.uint8)
    rgb = rgba[..., :3].astype(np.int32)
    alpha = rgba[..., 3].astype(np.int32)
    h, w = alpha.shape
    kind, bg = detect_background(rgb, alpha)

    if kind == "alpha":
        # Half-transparent pixels are soft shadows or antialiasing: keep only the solid ones.
        opaque = alpha >= 160
        if not opaque.any():
            return None
        if opts.shadow:
            # Darkish, colourless, not fully opaque pixels are a shadow.
            spread = rgb.max(axis=2) - rgb.min(axis=2)
            shadow = (alpha < 250) & (spread < 40) & (luminance(rgb) < 140)
            opaque &= ~shadow
        keep = opaque
    else:
        # Background: pixels close to the background colour, joined to the border of the picture.
        diff = np.abs(rgb - bg[None, None, :]).max(axis=2)
        if kind == "magenta":
            # Magenta and its antialiased / darkened shades: much more red and blue than green.
            cand = (rgb[..., 0] - rgb[..., 1] > 120) & (rgb[..., 2] - rgb[..., 1] > 120)
        else:
            cand = diff <= opts.bg_tolerance
        cand &= alpha >= 128
        cand |= alpha < 128
        seed = np.zeros_like(cand)
        seed[0, :] = seed[-1, :] = True
        seed[:, 0] = seed[:, -1] = True
        background = ndimage.binary_propagation(seed & cand, mask=cand)

        if opts.shadow and kind != "magenta":
            # Soft shadow: the background darkened (a grey, or the background hue), joined to the background,
            # in the lower half of the object (shadows lie on the ground).
            obj = ~background
            rows = np.where(obj.any(axis=1))[0]
            if len(rows) > 0:
                top, bottom = rows[0], rows[-1]
                below = np.zeros_like(obj)
                below[top + (bottom - top) // 2:, :] = True
                lum = luminance(rgb)
                bg_lum = float(luminance(bg.astype(np.float64)[None, None, :])[0, 0])
                spread = rgb.max(axis=2) - rgb.min(axis=2)
                shade = (lum < bg_lum - 4) & (lum > opts.shadow_min_luminance) & (spread <= opts.shadow_tolerance) & below
                # Only outside the silhouette of the solid object: left or right of its solid pixels in the row,
                # or below them in the column. Light parts of the object between its solid pixels stay.
                solid = obj & ~shade
                labels, count = ndimage.label(solid, structure=np.ones((3, 3)))
                if count:
                    sizes = ndimage.sum(solid, labels, index=np.arange(1, count + 1))
                    solid = (sizes >= sizes.max() * 0.05)[labels - 1] & (labels > 0)
                ys, xs = np.mgrid[0:h, 0:w]
                any_row = solid.any(axis=1)
                left = np.where(any_row, solid.argmax(axis=1), w)
                right = np.where(any_row, w - 1 - solid[:, ::-1].argmax(axis=1), -1)
                any_col = solid.any(axis=0)
                low = np.where(any_col, h - 1 - solid[::-1, :].argmax(axis=0), -1)
                outside = (xs < left[:, None]) | (xs > right[:, None]) | (ys > low[None, :])
                background = ndimage.binary_propagation(background, mask=background | cand | (shade & outside))
        keep = ~background

    if opts.hull and keep.any():
        # Everything inside the convex outline of the object is the object (white walls on a white background).
        from scipy.spatial import ConvexHull
        from PIL import ImageDraw
        pts = np.argwhere(ndimage.binary_opening(keep, iterations=1) if keep.sum() > 64 else keep)[:, ::-1]
        if len(pts) >= 3:
            hull = ConvexHull(pts)
            mask_img = Image.new("L", (w, h), 0)
            ImageDraw.Draw(mask_img).polygon([tuple(p) for p in pts[hull.vertices]], fill=1, outline=1)
            keep = np.array(mask_img, dtype=bool)

    # Drop specks: pieces much smaller than the largest one.
    labels, count = ndimage.label(keep, structure=np.ones((3, 3)))
    if count == 0:
        return None
    sizes = ndimage.sum(keep, labels, index=np.arange(1, count + 1))
    big = sizes >= max(4, sizes.max() * opts.speck)
    keep = big[labels - 1] & (labels > 0)
    # Fill tiny holes left inside the object by the shadow fill.
    holes = ndimage.binary_fill_holes(keep) & ~keep
    hole_labels, hole_count = ndimage.label(holes)
    if hole_count:
        hole_sizes = ndimage.sum(holes, hole_labels, index=np.arange(1, hole_count + 1))
        small = hole_sizes <= max(4, keep.sum() * 0.0005)
        keep |= small[hole_labels - 1] & (hole_labels > 0)

    out = rgba.copy()
    out[..., 3] = np.where(keep, 255, 0)
    out[~keep, :3] = 0
    ys, xs = np.where(keep)
    out = out[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    if debug_dir is not None:
        debug_dir.mkdir(parents=True, exist_ok=True)
        Image.fromarray(out, "RGBA").save(debug_dir / f"{name}_clean.png")
    return out


# ------------------------------------------------------------------------------------------------
# Scaling and outline
# ------------------------------------------------------------------------------------------------

def resize(arr, size, filt):
    img = Image.fromarray(arr, "RGBA")
    if filt == "nearest":
        out = img.resize(size, Image.NEAREST)
    else:
        # Box filter on premultiplied colours, then hard alpha again.
        out = img.resize(size, Image.BOX)
    a = np.array(out, dtype=np.uint8)
    a[..., 3] = np.where(a[..., 3] >= 128, 255, 0)
    a[a[..., 3] == 0, :3] = 0
    return a


def add_outline(arr, width, colour):
    """Dark outline of @p width pixels around the opaque pixels (on a canvas enlarged by @p width)."""
    if width <= 0:
        return arr
    h, w = arr.shape[:2]
    canvas = np.zeros((h + 2 * width, w + 2 * width, 4), dtype=np.uint8)
    canvas[width:width + h, width:width + w] = arr
    solid = canvas[..., 3] > 0
    ring = ndimage.binary_dilation(solid, structure=ndimage.generate_binary_structure(2, 1), iterations=width) & ~solid
    if colour is None:
        # Darkened colour of the nearest opaque neighbour.
        idx = ndimage.distance_transform_edt(~solid, return_distances=False, return_indices=True)
        near = canvas[idx[0], idx[1], :3].astype(np.int32)
        dark = (near * 0.3).astype(np.uint8)
        canvas[ring, :3] = dark[ring]
    else:
        canvas[ring, :3] = colour
    canvas[ring, 3] = 255
    return canvas


def diamond_mask(w, h):
    """The flat tile rhombus inside a w x h picture."""
    ys, xs = np.mgrid[0:h, 0:w]
    cx, cy = (w - 1) / 2, (h - 1) / 2
    return np.abs(xs - cx) / (w / 2) + np.abs(ys - cy) / (h / 2) <= 1.0


def fit(arr, box_w, box_h, anchor, opts):
    """Scale the cleaned picture to the game size of the object at one zoom (outline included)."""
    if anchor == "ground":
        out = resize(arr, (box_w, box_h), opts.filter)
        out[~diamond_mask(box_w, box_h), :] = 0
        return out
    pad = 2 * opts.outline
    inner_w, inner_h = max(1, box_w - pad), max(1, box_h - pad)
    h, w = arr.shape[:2]
    if anchor in ("building", "building2"):
        scale = inner_w / w
        if h * scale > inner_h:
            scale = inner_h / h
    else:
        scale = min(inner_w / w, inner_h / h)
    size = (max(1, round(w * scale)), max(1, round(h * scale)))
    out = add_outline(resize(arr, size, opts.filter), opts.outline, opts.outline_colour)
    if anchor == "icon":
        canvas = np.zeros((box_h, box_w, 4), dtype=np.uint8)
        oh, ow = out.shape[:2]
        y0, x0 = (box_h - oh) // 2, (box_w - ow) // 2
        canvas[y0:y0 + oh, x0:x0 + ow] = out
        return canvas
    return out


# ------------------------------------------------------------------------------------------------
# Commands
# ------------------------------------------------------------------------------------------------

def load_overrides(parser, opts):
    """Per-sprite cleaning options from raw/options.txt: lines "<name>: <options>" (# starts a comment)."""
    overrides = {}
    path = opts.raw / "options.txt"
    if not path.exists():
        return overrides
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if not line or ":" not in line:
            continue
        name, args = (part.strip() for part in line.split(":", 1))
        overrides[name] = parser.parse_args(args.split(), namespace=argparse.Namespace(**vars(opts)))
    return overrides


def convert(specs, base_opts, overrides):
    done, missing, mirrored, failed = 0, [], 0, []
    debug_dir = base_opts.out / "_debug" if base_opts.debug else None
    for spec in specs:
        opts = overrides.get(spec["name"], base_opts)
        if opts is not base_opts:
            print(f"     {spec['name']}: own options from raw/options.txt")
        if base_opts.only and base_opts.only not in spec["name"]:
            continue
        section = spec["section"]
        names = image_names(spec)
        src = {n: find_source(opts.raw / section, n) for n in names}
        if not any(src.values()):
            continue
        pictures = {}
        for n, p in src.items():
            if p is None:
                continue
            cleaned = clean(Image.open(p), opts, n, debug_dir)
            if cleaned is None:
                failed.append(f"{section}/{p.name}")
                print(f"empty after cleaning: {section}/{p.name}")
                continue
            pictures[n] = cleaned
        # Mirror the missing views only when at least 4 are drawn (a vehicle with fewer views keeps only those,
        # the game draws the others vanilla).
        drawn = len(pictures)
        for n in names:
            if n in pictures or (spec["kind"] == "dir8" and drawn < 4):
                continue
            partner = mirror_source(spec, n)
            if partner is not None and partner in pictures:
                pictures[n] = pictures[partner][:, ::-1].copy()
                mirrored += 1
        lacking = [n for n in names if n not in pictures]
        if lacking and spec["kind"] != "dir8":
            missing.extend(f"{section}/{n}" for n in lacking)
            print(f"skip {section}/{spec['name']}: missing {', '.join(lacking)}")
            continue
        names = [n for n in names if n in pictures]
        (opts.out / section).mkdir(parents=True, exist_ok=True)
        box_w, box_h = int(spec["width"]), int(spec["height"])
        for n in names:
            for z in ZOOMS:
                arr = fit(pictures[n], box_w * z, box_h * z, spec["anchor"], opts)
                Image.fromarray(arr, "RGBA").save(opts.out / section / f"{n}_{z}x.png")
        done += 1
        partial = f" (only {', '.join(n[len(spec['name']) + 1:] for n in names)}; other views vanilla)" if lacking else ""
        print(f"ok   {section}/{spec['name']}: {len(names)} image(s) x {len(ZOOMS)} zooms{partial}")
    print(f"\nconverted sprites: {done}, mirrored views: {mirrored}, missing files: {len(missing)}, failed: {len(failed)}")
    return 0 if not missing and not failed else 1


def print_list(specs):
    for spec in specs:
        names = image_names(spec)
        if spec["kind"] == "dir8":
            shown = f"{spec['name']}_{{N,NE,E,SE,S,SW,W,NW}} (enough: N, NE, E, SE)"
        elif spec["kind"] == "diag4x4":
            shown = f"{spec['name']}_{{NE,SE,SW,NW}}_{{0..3}} (enough: NE and SE)"
        elif spec["kind"] == "parts4":
            shown = f"{spec['name']}_{{0,1,2,3}}"
        else:
            shown = names[0]
        print(f"raw/{spec['section']}/{shown}.png  - {spec['what']}, {spec['width']}x{spec['height']} at 1x")


def print_inbox(specs):
    files = sorted(p for p in INBOX.iterdir() if p.suffix.lower() in EXTENSIONS) if INBOX.exists() else []
    if not files:
        print("inbox/ is empty")
        return 0
    print(f"{len(files)} picture(s) in inbox/ wait for a name. Copy each into raw/ with --assign, for example:")
    print(f"  python mod-sprites/metro_convert_sprites.py --assign \"inbox/{files[0].name}\" metro/train_a_head_SE")
    for p in files:
        with Image.open(p) as img:
            print(f"  {p.name}: {img.size[0]}x{img.size[1]} {img.mode}")
    return 0


def assign(specs, source, target):
    """Copy an inbox picture into raw/ under the name of one image of a sprite."""
    src = Path(source)
    if not src.is_absolute() and not src.exists():
        src = ROOT / source
    if not src.exists():
        sys.exit(f"no such file: {source}")
    section, _, name = target.partition("/")
    known = {(s["section"], n) for s in specs for n in image_names(s)}
    if (section, name) not in known:
        sys.exit(f"unknown image '{target}'; see --list (for example metro/train_a_head_SE, people/pedestrian_NE_0)")
    folder = RAW / section
    folder.mkdir(parents=True, exist_ok=True)
    for ext in EXTENSIONS:
        old = folder / f"{name}{ext}"
        if old.exists():
            old.unlink()
    dst = folder / f"{name}{src.suffix.lower()}"
    shutil.copyfile(src, dst)
    print(f"{src.name} -> {dst.relative_to(ROOT)}")
    return 0


def parse_colour(text):
    if text in (None, "", "auto"):
        return None
    text = text.lstrip("#")
    return tuple(int(text[i:i + 2], 16) for i in (0, 2, 4))


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")  # Russian descriptions in the Windows console.
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--list", action="store_true", help="print the files expected in raw/")
    parser.add_argument("--inbox", action="store_true", help="list the pictures waiting in inbox/")
    parser.add_argument("--assign", nargs=2, metavar=("PICTURE", "SECTION/NAME"), help="copy a picture into raw/ under a sprite name")
    parser.add_argument("--clean", action="store_true", help="delete game/ before converting")
    parser.add_argument("--only", default="", help="convert only the sprites whose name contains this text")
    parser.add_argument("--debug", action="store_true", help="also write the cleaned full-size pictures into game/_debug/")
    parser.add_argument("--raw", type=Path, default=RAW, help="folder with the source pictures (default: raw/)")
    parser.add_argument("--out", type=Path, default=GAME, help="folder for the game sprites (default: game/)")
    parser.add_argument("--bg-tolerance", type=int, default=24, help="how far from the background colour a pixel is still background (0-255, default 24)")
    parser.add_argument("--no-shadow", dest="shadow", action="store_false", help="keep soft shadows")
    parser.add_argument("--shadow-tolerance", type=int, default=32, help="how colourful a shadow pixel may be (max - min channel, default 32)")
    parser.add_argument("--shadow-min-luminance", type=int, default=40, help="darker pixels are never a shadow (default 40)")
    parser.add_argument("--hull", action="store_true", help="keep everything inside the convex outline of the object (white objects on white)")
    parser.add_argument("--speck", type=float, default=0.02, help="drop pieces smaller than this share of the largest one (default 0.02)")
    parser.add_argument("--outline", type=int, default=1, help="outline width in pixels at every zoom (default 1, 0 = none)")
    parser.add_argument("--outline-colour", default="auto", help="outline colour, e.g. 202020; 'auto' = darkened neighbour colour (default)")
    parser.add_argument("--filter", choices=("nearest", "box"), default="nearest", help="scaling filter (default nearest)")
    opts = parser.parse_args()
    opts.outline_colour = parse_colour(opts.outline_colour)
    specs = load_specs()
    if opts.list:
        print_list(specs)
        return 0
    if opts.inbox:
        return print_inbox(specs)
    if opts.assign:
        return assign(specs, *opts.assign)
    if opts.clean and opts.out.exists():
        shutil.rmtree(opts.out)
    overrides = load_overrides(parser, opts)
    for o in overrides.values():
        o.outline_colour = parse_colour(o.outline_colour) if isinstance(o.outline_colour, str) else o.outline_colour
    return convert(specs, opts, overrides)


if __name__ == "__main__":
    sys.exit(main())
