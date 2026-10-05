#!/usr/bin/env python3
"""Build the overworld sheets (assets/ow_*.png) and their sprite table.

Usage:
    python tools/build_overworld_sheets.py [--packs "../Asset packs"]
                                           [--dump <dir>]   # also write each quantized crop, for review

Why: the surface art comes from four packs (Pixel Crawler, High Tides,
LightBorne, Tiny Wonder Swamp). This is the one place that says which regions
ship and every edit made to them, so a palette change re-runs this script
instead of anyone touching a sheet by hand (docs/palette.md).

Every crop is quantized to assets/palette.json first (quantize.py: nearest in
OKLab), after two recorded edits: High Tides' flat sea is cut out of the coast
(our water shows through), and Pixel Crawler's red pine is recoloured rust.

Outputs, all the renderer loads:
  ow_water.png    8 generated 16px water fills (most plain, three with a dash)
  ow_coast.png    High Tides' sand coast: lake + island blocks, 3 foam frames
  ow_terrain.png  one row per inland material: the 16 corner tiles + fills
  ow_props.png    every tree, bush, rock and reed, repacked on the 16px grid
  src/render/overworld_sprites.hpp   where each prop sits in ow_props.png

CORNER TILES. The renderer autotiles on a dual grid: a tile is drawn on each
cell CORNER and shows the four cells that meet there, so its shape is one of
2^4 = 16 cases (a 4-bit mask: TL=8, TR=4, BL=2, BR=1). LightBorne's grass
ships exactly that set, diagonals included; the other packs draw their edges
as one irregular blob that is not a tileset. So LightBorne's tiles are the
MASKS for every material: a transition tile is the material's own fill
texture cut to LightBorne's shape, with LightBorne's outline pixels recoloured
to two darker shades of that material. Every biome edge therefore has the
same hand-drawn wobble, and no pack's mismatched edge art meets another's.

PROPS ON THE GRID. Pack sprites are not 16px-aligned. Each is cut to its
opaque bounds, then placed in a frame of whole tiles with its trunk - the
centre of its bottom rows - on the frame's centre line and its base on the
frame's bottom edge. The renderer then draws it with grid::srcTile and
grid::standingOn like any other sheet, and the trunk lands on its tile.

Requires numpy and Pillow. Deterministic.
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image

from palette_sample import srgb_to_oklab
from quantize import load_palette, quantize

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
T = 16  # art px per tile

PC = "Pixel Crawler - Free Pack 2.11/Pixel Crawler - Free Pack/Environment"
HT = "High Tides - Premium Pack"
LB = "LightBorne 1.1v"
SW = "Tiny Wonder Swamp"
TREES = PC + "/Props/Static/Trees"

# name -> (pack-relative source, crop box or None for the whole file)
SOURCES = {
    "coast":    (HT + "/Tilesets/Land_to_Sea Transitions.png", (0, 0, 128, 208)),  # sand only; grass/dirt coasts at x >= 128
    "beach":    (HT + "/Tilesets/Base_Tiles.png", (0, 0, 64, 48)),
    "floors":   (PC + "/Tilesets/Floors_Tiles.png", (0, 0, 160, 416)),
    "lb_tiles": (LB + "/Tilesets/Exterior/tiles.png", (0, 0, 528, 448)),
    "swamp":    (SW + "/swamp tilemap.png", (0, 0, 144, 160)),
    "lb_oak":   (LB + "/Environment/Vegetation/Trees/oak tree.png", None),
    "lb_birch": (LB + "/Environment/Vegetation/Trees/Birch tree.png", None),
    "lb_fir":   (LB + "/Environment/Vegetation/Trees/fir tree.png", None),
    "willow":   (SW + "/objects&items/swamp objects.png", (96, 0, 288, 208)),
    "palm":     (HT + "/Objects/Foliage.png", None),
    "lb_bush":  (LB + "/Environment/Vegetation/bushes.png", None),
    "lb_rock":  (LB + "/Environment/Deco/rocks.png", None),
    "pc_rock":  (PC + "/Props/Static/Rocks.png", (0, 16, 208, 304)),  # drops the "PALETTE:" label
    "reeds":    (SW + "/objects&items/swamp water objects.png", None),
}
for _m in (1, 2, 3):
    for _s in (2, 3, 4, 5):
        SOURCES[f"pc{_m}_s{_s}"] = (f"{TREES}/Model_0{_m}/Size_0{_s}.png", None)

CUT_OUT = {"coast": [(0x1D, 0xAF, 0xD9)]}
RECOLOUR = {f"pc3_s{s}": {(0x84, 0x20, 0x20): (0xAD, 0x52, 0x26)} for s in (2, 3, 4, 5)}

# ---- terrain ---------------------------------------------------------------

# LightBorne's grass corner set (lb_tiles, tiles 0-4 x 0-2): mask -> (col, row).
LB_CORNERS = {14: (0, 0), 12: (1, 0), 13: (2, 0), 8: (3, 0), 4: (4, 0),
              10: (0, 1), 5: (2, 1), 2: (3, 1), 1: (4, 1),
              11: (0, 2), 3: (1, 2), 7: (2, 2), 9: (3, 2), 6: (4, 2)}
LB_FILLS = [(12, 1)]  # its other fills differ in shade and checkerboard when mixed
# Every olive fill tile, holes and all: a corner-tile pixel in one of these
# colours is grass body, anything else is outline.
LB_BODY = [(c, r) for c in range(11, 20) for r in range(3)]

# Row order is the renderer's TerrainMaterial order. Fills are (sheet, col, row).
MATERIALS = [
    ("grass",   [("floors", 1, 10), ("floors", 2, 10), ("floors", 3, 10)]),
    ("forest",  [("lb_tiles", c, r) for c, r in LB_FILLS]),
    ("wetland", [("swamp", 3, 1)]),
    ("gravel",  [("floors", 6, 10), ("floors", 7, 10), ("floors", 8, 10)]),
    ("snow",    [("floors", 2, 14), ("floors", 3, 14), ("floors", 2, 15)]),
    ("sand",    [("beach", 1, 1), ("beach", 2, 1), ("beach", 1, 2), ("beach", 2, 2)]),  # fills only: the coast is its edge
]
FILL_COL = 16      # fills start here; columns 0-15 are the corner masks
FILLS_PER_ROW = 4  # fewer fills repeat

# ---- props -----------------------------------------------------------------
# (ID, sheet, box in that sheet's px). The box only has to contain the sprite
# and nothing else; it is tightened to the opaque pixels.


def grid_frames(sheet, w, h, names):
    """names[row][col] -> frame box; None skips a frame."""
    return [(n, sheet, (c * w, r * h, (c + 1) * w, (r + 1) * h))
            for r, row in enumerate(names) for c, n in enumerate(row) if n]


def boxes(sheet, items):
    return [(n, sheet, (x, y, x + w, y + h)) for n, (x, y, w, h) in items]


PROPS = (
    grid_frames("lb_oak", 80, 112, [["OAK_AUTUMN_A", "OAK_AUTUMN_B", "OAK_SUMMER_A", "OAK_SUMMER_B", "OAK_WINTER_A", "OAK_WINTER_B"],
                                    ["OAK_BARE_A", "OAK_BARE_B"]])
    + grid_frames("lb_birch", 80, 128, [["BIRCH_AUTUMN_A", "BIRCH_AUTUMN_B", "BIRCH_SUMMER_A", "BIRCH_SUMMER_B", "BIRCH_WINTER_A", "BIRCH_WINTER_B"],
                                        ["BIRCH_BARE_A", "BIRCH_BARE_B"]])
    + boxes("lb_fir", [("FIR_0", (3, 1, 77, 143)), ("FIR_1", (82, 17, 77, 127)), ("FIR_2", (169, 35, 61, 109)),
                       ("FIR_3", (255, 58, 50, 86)), ("FIR_4", (325, 87, 39, 56)),
                       ("FIR_DARK_0", (2, 214, 77, 105)), ("FIR_DARK_1", (81, 235, 61, 84)),
                       ("FIR_DARK_2", (151, 259, 51, 60)), ("FIR_DARK_3", (211, 273, 41, 46))])
    # Pixel Crawler model 1, broadleaf: summer green / tan, autumn orange / amber, bare, frozen.
    + boxes("pc1_s2", [("PC1_S2_GREEN", (16, 2, 48, 62)), ("PC1_S2_TAN", (80, 2, 48, 62)),
                       ("PC1_S2_BARE", (144, 17, 43, 47)), ("PC1_S2_FROZEN", (208, 18, 43, 46)),
                       ("PC1_S2_ORANGE", (16, 66, 48, 62)), ("PC1_S2_AMBER", (80, 66, 48, 62))])
    + grid_frames("pc1_s3", 48, 96, [["PC1_S3_GREEN", "PC1_S3_TAN", "PC1_S3_BARE", "PC1_S3_FROZEN"],
                                     ["PC1_S3_ORANGE", "PC1_S3_AMBER"]])
    + boxes("pc1_s4", [("PC1_S4_GREEN", (3, 2, 73, 126)), ("PC1_S4_TAN", (83, 2, 73, 126)),
                       ("PC1_S4_BARE", (166, 2, 69, 126)), ("PC1_S4_FROZEN", (246, 2, 69, 126)),
                       ("PC1_S4_ORANGE", (3, 130, 73, 126)), ("PC1_S4_AMBER", (83, 130, 73, 126))])
    + grid_frames("pc1_s5", 112, 160, [["PC1_S5_GREEN", "PC1_S5_TAN", "PC1_S5_BARE", "PC1_S5_FROZEN"],
                                       ["PC1_S5_ORANGE", "PC1_S5_AMBER"]])
    # Model 2, conifer: teal, green, bare.
    + boxes("pc2_s2", [("PC2_S2_TEAL", (3, 1, 29, 47)), ("PC2_S2_TEAL_B", (67, 1, 29, 47)),  # x < 32: the sapling
                       ("PC2_S2_GREEN", (3, 49, 29, 47)), ("PC2_S2_GREEN_B", (67, 49, 29, 47))])  # touches the tree
    + boxes("pc2_s3", [("PC2_S3_TEAL", (4, 4, 37, 76)), ("PC2_S3_TEAL_B", (52, 4, 37, 76)), ("PC2_S3_BARE", (99, 6, 40, 74)),
                       ("PC2_S3_GREEN", (4, 84, 37, 76)), ("PC2_S3_GREEN_B", (52, 84, 37, 76))])
    + boxes("pc2_s4", [("PC2_S4_TEAL", (4, 9, 54, 103)), ("PC2_S4_TEAL_B", (68, 9, 54, 103)), ("PC2_S4_BARE", (141, 17, 43, 95)),
                       ("PC2_S4_GREEN", (4, 121, 54, 103)), ("PC2_S4_GREEN_B", (68, 121, 54, 103))])
    + boxes("pc2_s5", [("PC2_S5_TEAL", (1, 3, 92, 157)), ("PC2_S5_TEAL_B", (97, 3, 92, 157)), ("PC2_S5_BARE", (197, 8, 87, 152)),
                       ("PC2_S5_GREEN", (1, 163, 92, 157)), ("PC2_S5_GREEN_B", (97, 163, 92, 157))])
    # Model 3, tall pine: green, olive, autumn tan, autumn rust (was red), bare.
    + boxes("pc3_s2", [("PC3_S2_GREEN", (0, 5, 30, 75)), ("PC3_S2_OLIVE", (64, 5, 30, 75)),
                       ("PC3_S2_TAN", (0, 85, 30, 75)), ("PC3_S2_RUST", (64, 85, 30, 75))])
    + grid_frames("pc3_s3", 64, 144, [["PC3_S3_GREEN", "PC3_S3_OLIVE"], ["PC3_S3_TAN", "PC3_S3_RUST"]])
    + boxes("pc3_s3", [("PC3_S3_BARE", (137, 7, 47, 137))])
    + grid_frames("pc3_s4", 96, 208, [["PC3_S4_GREEN", "PC3_S4_OLIVE"], ["PC3_S4_TAN", "PC3_S4_RUST"]])
    + boxes("pc3_s4", [("PC3_S4_BARE", (202, 14, 65, 194))])
    + grid_frames("pc3_s5", 128, 256, [["PC3_S5_GREEN", "PC3_S5_OLIVE"], ["PC3_S5_TAN", "PC3_S5_RUST"]])
    + boxes("pc3_s5", [("PC3_S5_BARE", (271, 5, 98, 251))])
    + boxes("willow", [("WILLOW_LIT", (0, 0, 80, 96)), ("WILLOW", (80, 0, 80, 96)),
                       ("WILLOW_S_A", (16, 96, 48, 64)), ("WILLOW_S_B", (64, 96, 48, 64)), ("WILLOW_S_C", (112, 96, 48, 64))])
    + boxes("palm", [("PALM_TALL", (53, 12, 36, 84)), ("PALM_SHORT", (7, 39, 33, 57))])
    + boxes("lb_bush", [("BUSH_A", (16, 99, 32, 29)), ("BUSH_B", (81, 103, 28, 25)), ("BUSH_C", (16, 147, 32, 29)),
                        ("BUSH_D", (81, 148, 31, 27)), ("BUSH_LOW", (1, 32, 31, 16)), ("BUSH_SMALL", (34, 3, 13, 13))])
    + boxes("lb_rock", [("ROCK_GREY_0", (3, 7, 10, 8)), ("ROCK_GREY_1", (17, 4, 14, 11)), ("ROCK_GREY_2", (33, 1, 31, 14)),
                        ("ROCK_GREY_3", (66, 2, 13, 13)), ("ROCK_GREY_4", (81, 2, 14, 13)), ("ROCK_GREY_BIG", (99, 12, 26, 19)),
                        ("ROCK_MOSS_1", (17, 20, 14, 11)), ("ROCK_MOSS_2", (33, 17, 31, 14)), ("ROCK_MOSS_3", (66, 18, 13, 13))])
    + boxes("pc_rock", [("BOULDER_BROWN", (2, 3, 28, 43)), ("BOULDER_BROWN_LOW", (35, 3, 26, 27)),
                        ("BOULDER_GREY", (98, 3, 28, 43)), ("BOULDER_GREY_LOW", (131, 3, 26, 27))])
    + boxes("reeds", [("CATTAIL_TALL", (48, 16, 16, 32)), ("CATTAIL_SHORT", (64, 16, 16, 32)), ("SWAMP_PLANT", (80, 48, 16, 32))])
)
ATLAS_TILES = 40  # atlas width in tiles


def match(a, rgb):
    return (a[..., 0] == rgb[0]) & (a[..., 1] == rgb[1]) & (a[..., 2] == rgb[2])


def load_sources(packs, pal):
    out = {}
    for name, (src, box) in SOURCES.items():
        img = Image.open(os.path.join(packs, src)).convert("RGBA")
        if box:
            img = img.crop(box)
        px = np.array(img)
        for rgb in CUT_OUT.get(name, []):
            px[match(px, rgb), 3] = 0
        for old, new in RECOLOUR.get(name, {}).items():
            px[match(px, old), :3] = new
        out[name] = np.array(quantize(Image.fromarray(px, "RGBA"), *pal))
    return out


def tile(a, col, row):
    return a[row * T:(row + 1) * T, col * T:(col + 1) * T]


def water_sheet():
    """8 seamless fills, five plain and three with one short light dash -
    Pixel Crawler's open-water ripples as they look on the palette. Dashes stay
    off the tile edge, so any two variants tile seamlessly."""
    base, light = (0x4E, 0x91, 0xAF, 255), (0x6E, 0xA7, 0xC6, 255)
    dashes = {5: (3, 4, 3), 6: (9, 10, 2), 7: (5, 12, 4)}  # variant: (x, y, length)
    a = np.zeros((T, 8 * T, 4), np.uint8)
    a[:] = base
    for v, (x, y, n) in dashes.items():
        a[y, v * T + x:v * T + x + n] = light
    return a


def nearest_rgb(lab, pal):
    rgb, plab = pal
    return rgb[np.argmin(np.linalg.norm(plab - lab, axis=1))]


def terrain_sheet(src, pal):
    lb = src["lb_tiles"]
    lb_body = {tuple(px[:3]) for c, r in LB_BODY for px in tile(lb, c, r).reshape(-1, 4) if px[3]}
    rows = []
    for name, fills in MATERIALS:
        row = np.zeros((T, (FILL_COL + FILLS_PER_ROW) * T, 4), np.uint8)
        fill_tiles = [tile(src[s], c, r) for s, c, r in fills]
        for i in range(FILLS_PER_ROW):
            row[:, (FILL_COL + i) * T:(FILL_COL + i + 1) * T] = fill_tiles[i % len(fill_tiles)]
        if name != "sand":
            row[:, 15 * T:16 * T] = fill_tiles[0]
            # The material's outline: its fill's mean colour, darkened twice in
            # OKLab along the same hue, each snapped to the palette.
            body = fill_tiles[0].reshape(-1, 4)
            mean = srgb_to_oklab(body[body[:, 3] > 0][:, :3]).mean(0)
            shades = [nearest_rgb(mean * [1, k, k] - [d, 0, 0], pal) for d, k in ((0.24, 1.1), (0.12, 1.05))]
            for mask, (c, r) in LB_CORNERS.items():
                m = tile(lb, c, r)
                out = np.zeros_like(m)
                for y in range(T):
                    for x in range(T):
                        if m[y, x, 3] == 0:
                            continue
                        if tuple(m[y, x, :3]) in lb_body:
                            # Body from the fill tile itself, so the edge and
                            # the fill beside it are the same texture.
                            out[y, x] = fill_tiles[0][y, x]
                        elif name == "forest":
                            out[y, x] = m[y, x]  # LightBorne's own outline
                        else:  # outline pixel: dark or mid by its own lightness
                            L = srgb_to_oklab(m[y, x, :3][None].astype(np.uint8))[0, 0]
                            out[y, x, :3] = shades[0] if L < 0.45 else shades[1]
                            out[y, x, 3] = 255
                row[:, mask * T:(mask + 1) * T] = out
        rows.append(row)
    return np.concatenate(rows, axis=0)


def main_body(a):
    """Clears every opaque island (8-connected) that lies wholly outside the
    largest one's bounding box: the saplings Pixel Crawler draws beside its
    small trees. Loose pixels inside the box (twigs, lantern lights) stay."""
    op = a[..., 3] > 0
    label = np.zeros(op.shape, np.int32)
    boxes_ = []
    for y0, x0 in zip(*np.nonzero(op)):
        if label[y0, x0]:
            continue
        n = len(boxes_) + 1
        stack, label[y0, x0] = [(y0, x0)], n
        ys, xs, size = [y0], [x0], 0
        while stack:
            y, x = stack.pop()
            size += 1
            ys.append(y), xs.append(x)
            for yy in range(max(0, y - 1), min(op.shape[0], y + 2)):
                for xx in range(max(0, x - 1), min(op.shape[1], x + 2)):
                    if op[yy, xx] and not label[yy, xx]:
                        label[yy, xx] = n
                        stack.append((yy, xx))
        boxes_.append((size, min(xs), min(ys), max(xs), max(ys)))
    _, bx0, by0, bx1, by1 = max(boxes_)
    out = a.copy()
    for n, (_, x0, y0, x1, y1) in enumerate(boxes_, 1):
        if x1 < bx0 or x0 > bx1 or y1 < by0 or y0 > by1:
            out[label == n] = 0
    return out


def props_atlas(src):
    """Repack every prop into whole-tile frames; returns the atlas and the
    frame (col, row, w, h) of each prop in PROPS order."""
    sprites = []
    for name, sheet, (x0, y0, x1, y1) in PROPS:
        a = src[sheet][y0:y1, x0:x1].copy()
        a[a[..., 3] < 128] = 0  # LightBorne's faint drop shadows read as a box halo
        a = main_body(a)
        ys, xs = np.nonzero(a[..., 3] > 0)
        a = a[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
        h, w = a.shape[:2]
        # Trunk = centre of the opaque pixels in the bottom three rows.
        bottom = np.nonzero(a[h - 3:, :, 3] > 0)[1]
        ax = (bottom.min() + bottom.max() + 1) // 2
        half = max(ax, w - ax)
        fw = -(-2 * half // T)        # tiles wide, rounded up
        fh = -(-h // T)
        frame = np.zeros((fh * T, fw * T, 4), np.uint8)
        left = fw * T // 2 - ax
        frame[fh * T - h:, left:left + w] = a
        sprites.append((name, frame))
    # Shelf packing, tallest first; ids keep PROPS order.
    order = sorted(range(len(sprites)), key=lambda i: -sprites[i][1].shape[0])
    place, x, y, shelf = {}, 0, 0, 0
    for i in order:
        fw, fh = sprites[i][1].shape[1] // T, sprites[i][1].shape[0] // T
        if x + fw > ATLAS_TILES:
            x, y, shelf = 0, y + shelf, 0
        place[i] = (x, y, fw, fh)
        x, shelf = x + fw, max(shelf, fh)
    atlas = np.zeros(((y + shelf) * T, ATLAS_TILES * T, 4), np.uint8)
    for i, (c, r, fw, fh) in place.items():
        atlas[r * T:(r + fh) * T, c * T:(c + fw) * T] = sprites[i][1]
    return atlas, [place[i] for i in range(len(sprites))]


def write_header(frames, path):
    names = [p[0] for p in PROPS]
    tallest = max(f[3] for f in frames)
    out = ["#pragma once",
           "// ==== owsprite ====",
           "// Where every overworld prop sits in assets/ow_props.png, in whole tiles.",
           "// GENERATED by tools/build_overworld_sheets.py - edit the PROPS table there",
           "// and re-run, never this file. Each frame's trunk is on its centre line and",
           "// its base on its bottom edge, so grid::standingOn(frame, x, y) roots it on",
           "// tile (x, y).", "",
           "namespace owsprite {", "",
           "struct Frame {", "  int col, row, w, h;", "};", "",
           "enum Id : int {"]
    out += [f"  {n}," for n in names]
    out += ["  COUNT", "};", "",
            f"// The tallest frame, in tiles: how far below the screen a prop can be",
            f"// rooted and still reach into view.",
            f"inline constexpr int kTallestTiles = {tallest};", "",
            "inline constexpr Frame kFrames[COUNT] = {"]
    out += [f"    {{{c}, {r}, {w}, {h}}}, // {n}" for n, (c, r, w, h) in zip(names, frames)]
    out += ["};", "", "} // namespace owsprite", ""]
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(out))


def save(a, path):
    Image.fromarray(a, "RGBA").save(path)
    print(f"wrote {os.path.relpath(path, ROOT)} {a.shape[1]}x{a.shape[0]}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--packs", default=os.path.join(os.path.dirname(ROOT), "Asset packs"))
    ap.add_argument("--palette", default=os.path.join(ROOT, "assets", "palette.json"))
    ap.add_argument("--dump", help="also write every quantized crop here, for review")
    a = ap.parse_args()
    pal = load_palette(a.palette)
    src = load_sources(a.packs, pal)
    if a.dump:
        os.makedirs(a.dump, exist_ok=True)
        for name, px in src.items():
            Image.fromarray(px, "RGBA").save(os.path.join(a.dump, name + ".png"))

    assets = os.path.join(ROOT, "assets")
    save(water_sheet(), os.path.join(assets, "ow_water.png"))
    save(src["coast"], os.path.join(assets, "ow_coast.png"))
    save(terrain_sheet(src, pal), os.path.join(assets, "ow_terrain.png"))
    atlas, frames = props_atlas(src)
    save(atlas, os.path.join(assets, "ow_props.png"))
    write_header(frames, os.path.join(ROOT, "src", "render", "overworld_sprites.hpp"))
    print(f"wrote src/render/overworld_sprites.hpp: {len(frames)} props")
    return 0


if __name__ == "__main__":
    sys.exit(main())
