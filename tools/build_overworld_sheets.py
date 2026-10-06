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
  ow_water.png    still water (lakes, the sea): one 64px square that tiles
                  with itself, with light dashes and a few lighter ones; the
                  renderer samples it by world position plus a slow
                  diagonal sway
  ow_swamp_water.png  swamp water, the same way: murk flecked with duckweed,
                  in four 64px squares stacked down the sheet - three tints
                  a quarter, half and three quarters of the way from lake
                  blue to the murk, then the murk itself. The renderer steps
                  through them away from open water
  ow_glints.png   a glint's three frames (a spark, a cross, a spark)
  ow_steps.png    what feet leave (StepEffects): a snow boot print per
                  facing, each again half filled in; then the wetland
                  squish's four frames; then the sand kick's four frames.
                  Drawn by hand in STEP_INK colours
  ow_river.png    flowing water: one 64px square that tiles with itself, which
                  the renderer scrolls downstream (dashes a little denser than
                  still water's, so the motion reads)
  ow_coast.png    High Tides' sand coast: lake + island blocks, 3 foam frames
  ow_shades.png   shade overlays (grass -> meadow -> forest floor): 81 tiles
                  per row, one per 3-state corner combination, + fills
  ow_terrain.png  grass (the 16 corner tiles + fills), sand fills, and three
                  generated mud BANK rows: LightBorne's shapes grown 4 px into
                  the water, the lip a lake or river shows past the grass -
                  plain mud, then four steps paler up the brown ramp to the
                  sand's own tan, for banks running into a beach
  ow_fades.png    the FADE materials the renderer's shader draws: one row
                  each (wetland, gravel, snow, snow drift, dune sand) of 8
                  fills (the wetland's strewn with the swamp pack's blades), then
                  the material's two outline colours as pixels (x = 128,
                  129); a last row of LightBorne's 16 corner shapes coded
                  body / mid outline / dark outline, which the shader clips
                  land materials to. Row 0 (swamp water) is empty: the shader
                  draws it from ow_swamp_water.png
  ow_props.png    every tree, bush, rock and reed, repacked on the 16px grid
  ow_props_wet.png  each prop's bottom tile row twice more, for roots that
                  spill onto water: with a foam line at the waterline, and
                  sunk (lower pixels dithered away). The renderer laps
                  between dry, foam and sunk
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

FADES. Wetland, gravel, snow and drifts have no corner tiles:
the renderer's shader draws them pixel by pixel, dithered by how much of
each lies around a tile, so they blend across several tiles. Where land
meets water it clips them to LightBorne's shapes and colours the outline.

SHADE OVERLAYS. A shade sits on a base material (forest floor on grass) and
fades out over a dithered band. Its corners have THREE states - not on the
base (water, beach), base only, shaded - because the overlay must dither
toward plain base but cut cleanly, with the base's own outline, against
anything else; otherwise every forest shore would show a band of grass.
3^4 = 81 tiles per overlay, indexed TL*27 + TR*9 + BL*3 + BR.

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
    "lb_flora": (LB + "/Environment/Vegetation/grass, flowers & mushrooms.png", None),
    "lb_wet":   (LB + "/Environment/Deco/deco on water.png", None),
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
# Every olive fill tile, holes and all: a corner-tile pixel in one of these
# colours is grass body, anything else is outline.
LB_BODY = [(c, r) for c in range(11, 20) for r in range(3)]

# Where each material's fill tiles come from: (sheet, col, row).
MATERIALS = [
    ("grass",   [("floors", 1, 10), ("floors", 2, 10), ("floors", 3, 10)]),
    ("wetland", [("swamp", 3, 1)]),
    ("gravel",  [("floors", 6, 10), ("floors", 7, 10), ("floors", 8, 10)]),
    ("snow",    []),  # generated: snow_fills
    ("sand",    [("beach", 1, 1), ("beach", 2, 1), ("beach", 1, 2), ("beach", 2, 2)]),  # fills only: the coast is its edge
]
TERRAIN = ["grass", "sand"]  # ow_terrain.png rows, then the bank rows
FADES = ["swamp", "wetland", "gravel", "snow", "drift", "dune"]  # ow_fades.png rows
FADE_FILLS = 8
# The bank rows' colours, mud first and then one brown-ramp step at a time
# toward the sand (#d8b289 is the coast's own darker sand), one row per tile
# nearer a beach: (lip, lip shadow, wet line at the waterline).
BANK_STEPS = [("#7d4c2e", "#663b27", "#4a2a19"),
              ("#936340", "#7d4c2e", "#663b27"),
              ("#b17a4e", "#936340", "#7d4c2e"),
              ("#cc9770", "#b17a4e", "#936340"),
              ("#d8b289", "#cc9770", "#b17a4e")]
FILL_COL = 16      # fills start here; columns 0-15 are the corner masks
FILLS_PER_ROW = 4  # fewer fills repeat

# Shade overlays (ow_shades.png), one row each, drawn over their base
# material where Island::sample's shade says so. Each is the base's fills
# recoloured: grass fades to forest floor in three steps (#55834c and
# #53763e were added to the palette for them). Row order is the renderer's
# shade-overlay order.
SHADES = [
    ("meadow",      "grass", {"#3d7f41": "#55834c"}),
    ("forest",      "grass", {"#3d7f41": "#6a8657", "#3c6723": "#53763e"}),
    ("forest_deep", "grass", {"#3d7f41": "#53763e"}),
]
# Snow drifts: snow's fills recoloured grey, a fade material of their own.
DRIFT = {"#dfe3ed": "#d9d9da", "#d9d9da": "#bdd5de", "#bdd5de": "#a6b7c6"}
# The fade sheet's corner shapes, coded in palette colours by red channel.
MASK_BODY, MASK_MID, MASK_DARK = "#dfe3ed", "#7e7e7e", "#141517"
SHADE_BAND = 6.0  # px over which an overlay dithers out

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
    + boxes("willow", [("WILLOW", (80, 0, 80, 96)),  # x < 80: the lantern-hung willow, not used
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
    # Ground details (DECAL_*): flat, walk-over, drawn with the terrain.
    + boxes("lb_flora", [("DECAL_FLOWERS_A", (65, 1, 13, 12)), ("DECAL_FLOWERS_B", (81, 1, 10, 13)),
                         ("DECAL_FLOWER", (49, 2, 8, 8)), ("DECAL_STARS_A", (68, 17, 15, 14)),
                         ("DECAL_STARS_B", (96, 17, 16, 14)), ("DECAL_TUFT_A", (14, 64, 20, 15)),
                         ("DECAL_TUFT_B", (15, 49, 18, 14)), ("DECAL_PATCH_A", (48, 17, 15, 13)),
                         ("DECAL_PATCH_B", (33, 20, 13, 10)), ("DECAL_PATCH_C", (2, 17, 14, 9)),
                         ("DECAL_TWIGS", (96, 65, 16, 15)), ("DECAL_FERN", (127, 64, 18, 16)),
                         ("DECAL_MUSHROOM_RED", (99, 81, 10, 13)), ("DECAL_MUSHROOM_BROWN", (99, 97, 10, 13)),
                         ("DECAL_MUSHROOMS", (128, 113, 16, 14))])
    + boxes("lb_rock", [("DECAL_PEBBLE_GREY", (3, 7, 10, 8)), ("DECAL_PEBBLES_GREY", (17, 4, 14, 11)),
                        ("DECAL_PEBBLE_MOSS", (3, 23, 10, 8)), ("DECAL_PEBBLE_BROWN", (3, 39, 10, 8))])
    + boxes("lb_wet", [("DECAL_STONE_WATER_A", (1, 35, 15, 12)), ("DECAL_STONE_WATER_B", (17, 49, 15, 14)),
                       ("DECAL_STONE_WATER_C", (2, 66, 13, 13)), ("DECAL_STONE_WATER_MOSS", (49, 35, 15, 12))])
    + boxes("gen", [("DECAL_DEAD_TUFT_A", (0, 0, 20, 15)), ("DECAL_DEAD_TUFT_B", (24, 0, 19, 14)),
                    ("DECAL_ICE_A", (48, 0, 14, 7)), ("DECAL_ICE_B", (64, 0, 10, 5)),
                    ("DECAL_SHELL", (80, 0, 6, 5)), ("DECAL_SHELL_PINK", (88, 0, 5, 4)),
                    ("DECAL_PEBBLE_SNOW", (96, 0, 10, 8)), ("DECAL_PEBBLES_SNOW", (112, 0, 14, 11)),
                    ("DECAL_STICK", (128, 0, 12, 5)), ("DECAL_MOUND_A", (144, 0, 14, 7)),
                    ("DECAL_MOUND_B", (160, 0, 10, 5))])
)
# Per-sprite recolours after quantizing. Pixel Crawler's broadleaf greens
# are a lime that glows against every other tree: one step darker each.
# High Tides' palm fronds quantize to the accent lime: same treatment.
PROP_RECOLOUR = {f"PC1_S{s}_GREEN": {"#91ca51": "#69a754", "#69a754": "#55834c"} for s in (2, 3, 4, 5)}
PROP_RECOLOUR.update({n: {"#95da41": "#69a754"} for n in ("PALM_TALL", "PALM_SHORT")})
ATLAS_TILES = 40  # atlas width in tiles
WATERLINE = 11    # art-px row of a prop's bottom tile the lapping water reaches
# Props whose roots spread far enough over a river to lap (the widest, at
# 30-32 px across; the next widest are 23). The rest draw dry over water.
LAPPING = {"WILLOW"} | {f"PC1_S5_{c}" for c in ("GREEN", "TAN", "BARE", "FROZEN", "ORANGE", "AMBER")}


def snow_cap(rock):
    """A pebble under snow: each column's top three pixels become a pale
    outline, snow, and the snow's shadow."""
    out = rock.copy()
    out[out[..., 3] < 128] = 0
    for x in range(out.shape[1]):
        ys = np.nonzero(out[:, x, 3] > 0)[0]
        for k, y in enumerate(ys[:3]):
            out[y, x, :3] = hexrgb(("#a6b7c6", "#dfe3ed", "#bdd5de")[k])
    return out


def snow_mound(w, h):
    """A small heap of snow. Its top is the ground's own white, so it shows
    only by its shaded side and a darker foot."""
    yy, xx = np.mgrid[0:h, 0:w]
    u, v = (xx - (w - 1) / 2) / (w / 2), (yy - (h - 1) / 2) / (h / 2)
    inside = u * u + v * v <= 1.0
    below = np.zeros_like(inside)
    below[:-1] = inside[1:]
    a = np.zeros((h, w, 4), np.uint8)
    a[inside & (u + 0.7 * v > 0.45)] = (*hexrgb("#bdd5de"), 255)
    a[inside & ~below & (v > 0)] = (*hexrgb("#bdd5de"), 255)
    a[inside & ~below & (v > 0.4) & (u > -0.3)] = (*hexrgb("#a6b7c6"), 255)
    return a


def pixels(rows, colours):
    """A tiny sprite from strings; '.' (or any unlisted char) is clear."""
    a = np.zeros((len(rows), len(rows[0]), 4), np.uint8)
    for y, row in enumerate(rows):
        for x, c in enumerate(row):
            if c in colours:
                a[y, x] = (*hexrgb(colours[c]), 255)
    return a


def generated_decals(src):
    """Decals no pack draws: dead grass (LightBorne's tufts, each green moved
    to the straw tan of the same lightness), ice patches, two beach shells,
    and the snow set - snow-capped pebbles, a fallen stick, snow mounds."""
    g = np.zeros((16, 176, 4), np.uint8)
    straw = [hexrgb(h) for h in ("#544527", "#74653c", "#96894e", "#b4ac6a")]
    straw_L = srgb_to_oklab(np.array(straw, np.uint8))[:, 0]
    for (x0, y0, w, h), dx in (((14, 64, 20, 15), 0), ((15, 80, 19, 14), 24)):
        tuft = src["lb_flora"][y0:y0 + h, x0:x0 + w].copy()
        op = tuft[..., 3] > 0
        L = srgb_to_oklab(tuft[op][:, :3])[:, 0]
        # Spread the tuft's lightness over the four straws, darkest to lightest.
        rank = np.clip(((L - L.min()) / max(1e-6, L.max() - L.min()) * 4).astype(int), 0, 3)
        tuft[op, :3] = np.array(straw, np.uint8)[rank]
        g[0:h, dx:dx + w] = tuft
    ice, rim, glint = hexrgb("#b4c4e4"), hexrgb("#a6b7c6"), hexrgb("#dfe3ed")
    for x0, w, h in ((48, 14, 7), (64, 10, 5)):
        yy, xx = np.mgrid[0:h, 0:w]
        inside = ((xx - (w - 1) / 2) / (w / 2)) ** 2 + ((yy - (h - 1) / 2) / (h / 2)) ** 2 <= 1.0
        patch = np.zeros((h, w, 4), np.uint8)
        patch[inside] = (*ice, 255)
        patch[inside & (yy == h - 1)] = (*rim, 255)
        patch[1, w // 3:w // 3 + 3] = (*glint, 255)
        g[0:h, x0:x0 + w] = patch
    shell, shade, pink = hexrgb("#f6e998"), hexrgb("#cc9770"), hexrgb("#f4a568")
    sh = np.zeros((5, 6, 4), np.uint8)
    for y, row in enumerate(("..##..", ".#..#.", "######", "#.##.#", ".####.")):
        for x, c in enumerate(row):
            if c == "#":
                sh[y, x] = (*(shade if y >= 3 and x in (0, 5) else shell), 255)
    sh[1, 2:4] = (*shade, 255)
    g[0:5, 80:86] = sh
    pk = np.zeros((4, 5, 4), np.uint8)
    for y, row in enumerate((".###.", "#####", "#.#.#", ".###.")):
        for x, c in enumerate(row):
            if c == "#":
                pk[y, x] = (*(pink if y < 2 else shade), 255)
    g[0:4, 88:93] = pk
    for (x0, y0, w, h), dx in (((3, 7, 10, 8), 96), ((17, 4, 14, 11), 112)):
        g[0:h, dx:dx + w] = snow_cap(src["lb_rock"][y0:y0 + h, x0:x0 + w])
    g[0:5, 128:140] = pixels(("......L.....", ".....D......", "LLLLLDLLLL..",
                              "DDDDDDDDDDLL", ".sssssssss.."),
                             {"L": "#7d4c2e", "D": "#4a2a19", "s": "#bdd5de"})
    g[0:7, 144:158] = snow_mound(14, 7)
    g[0:5, 160:170] = snow_mound(10, 5)
    return g


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


BAYER4 = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]])


def dist_to(region):
    """Per pixel, the Euclidean distance (px) to the nearest pixel of
    `region` inside the tile; large where region is empty."""
    ys, xs = np.nonzero(region)
    if len(ys) == 0:
        return np.full((T, T), 99.0)
    yy, xx = np.mgrid[0:T, 0:T]
    d = np.sqrt((yy[..., None] - ys) ** 2 + (xx[..., None] - xs) ** 2)
    return d.min(-1)


def dashed_water(count, seed, tones):
    """64x64 of open water with `count` short dashes, seamless (dashes wrap
    round the edges and keep a 2 px margin from each other, so none merge).
    `tones`: [(colour, share)] - each dash picks one by its share."""
    n = 4 * T
    a = np.zeros((n, n, 4), np.uint8)
    a[:] = (0x4E, 0x91, 0xAF, 255)
    rng = np.random.default_rng(seed)
    taken = np.zeros((n, n), bool)
    placed = 0
    while placed < count:
        x, y, length = int(rng.integers(0, n)), int(rng.integers(0, n)), int(rng.integers(2, 5))
        xs = [(x + i) % n for i in range(length)]
        near = [((y + dy) % n, (xx + dx) % n) for xx in xs for dy in (-2, -1, 0, 1, 2) for dx in (-2, 0, 2)]
        if any(taken[p] for p in near):
            continue
        colour = tones[-1][0]
        if len(tones) > 1:  # one tone needs no roll
            r = rng.random()
            for c, share in tones:
                if r < share:
                    colour = c
                    break
                r -= share
        for xx in xs:
            a[y, xx] = (*hexrgb(colour), 255)
            taken[y, xx] = True
        placed += 1
    return a


def water_sheet():
    """Still water: Pixel Crawler's ripples as they look on the palette, as
    sparse as the old per-tile fills (about one dash in three tiles), one in
    four of them a shade lighter."""
    return dashed_water(6, 17, [("#91d6e8", 0.25), ("#6ea7c6", 0.75)])


# Swamp water's tints, open water toward full murk: the murk (and what the
# duckweed and ripples become) per step. The blends are the OKLab mixes of
# lake blue #4e91af and murk #41564b at 1/4, 1/2 and 3/4, added to the
# palette for this; the faintest step has no duckweed yet.
SWAMP_BLENDS = ["#4b8295", "#48737c", "#456563"]
SWAMP_TINTS = [
    {"#41564b": "#4b8295", "#3d7f41": "#4b8295", "#6a8657": "#4b8295", "#576477": "#6ea7c6"},
    {"#41564b": "#48737c", "#3d7f41": "#456563", "#576477": "#4b8295"},
    {"#41564b": "#456563"},
    {},
]


def swamp_water_sheet():
    """Swamp water: the swamp fills laid 4 x 4 in a fixed shuffle (their
    flecks stay off the tile edges, so the square tiles with itself), once
    per tint, the squares stacked down the sheet."""
    fills = swamp_fills()
    order = np.random.default_rng(41).permutation(16) % len(fills)
    a = np.zeros((4 * T, 4 * T, 4), np.uint8)
    for i, v in enumerate(order):
        a[i // 4 * T:(i // 4 + 1) * T, i % 4 * T:(i % 4 + 1) * T] = fills[v]
    return np.concatenate([recolour(a, tint) for tint in SWAMP_TINTS], axis=0)


def glints_sheet():
    """A glint, three frames on 16px tiles: a light spark, a cross with a
    white heart, the spark again. Centred at (7, 7)."""
    spark, white = (*hexrgb("#91d6e8"), 255), (*hexrgb("#dfe3ed"), 255)
    a = np.zeros((T, 3 * T, 4), np.uint8)
    for f in (0, 2):
        a[7, f * T + 7] = spark
    a[7, T + 6:T + 9] = spark
    a[6:9, T + 7] = spark
    a[7, T + 7] = white
    return a


def swamp_fills():
    """Swamp water: murky grey-green, flecked with duckweed."""
    murk, weed, weed_lit, ripple = (0x41, 0x56, 0x4B), (0x3D, 0x7F, 0x41), (0x6A, 0x86, 0x57), (0x57, 0x64, 0x77)
    rng = np.random.default_rng(7)
    fills = []
    for v in range(8):
        f = np.zeros((T, T, 4), np.uint8)
        f[:] = (*murk, 255)
        # A few duckweed clumps (a dark fleck with a lit pixel) and, on some
        # variants, one dull ripple. Kept off the edge so fills tile.
        for _ in range(rng.integers(0, 3)):
            x, y = rng.integers(2, T - 3, 2)
            f[y, x:x + 2, :3] = weed
            f[y - 1, x, :3] = weed_lit
        if v % 3 == 2:
            x, y = rng.integers(2, T - 6), rng.integers(3, T - 3)
            f[y, x:x + 3, :3] = ripple
        fills.append(f)
    return fills


def river_sheet():
    """Flowing water: the still water's dashes and tones, denser so the
    motion reads. The renderer samples it by world position plus a scroll, so
    neighbouring river tiles show one continuous surface."""
    return dashed_water(14, 31, [("#91d6e8", 0.25), ("#6ea7c6", 0.75)])


def bank_row(lb, colours):
    """Mud bank: each LightBorne corner shape grown 4 px into the water. The
    grass drawn over it hides the original shape, so what shows is the lip:
    three px of mud and one of dark wet mud at the waterline. Fills are mud
    too (seen only where nothing covers them). `colours` is one BANK_STEPS
    entry: the paler ones are for banks near a beach, so mud fades into the
    sandy coast."""
    rng = np.random.default_rng(11)
    mud, mud_dark, wet = (hexrgb(h) for h in colours)
    row = np.zeros((T, (FILL_COL + FILLS_PER_ROW) * T, 4), np.uint8)
    fill = np.zeros((T, T, 4), np.uint8)
    fill[:] = (*mud, 255)
    for _ in range(10):
        x, y = rng.integers(0, T, 2)
        fill[y, x, :3] = mud_dark
    for i in range(FILLS_PER_ROW):
        row[:, (FILL_COL + i) * T:(FILL_COL + i + 1) * T] = np.roll(fill, (5 * i, 7 * i), (0, 1))
    row[:, 15 * T:16 * T] = fill
    for mask, (c, r) in LB_CORNERS.items():
        shape = tile(lb, c, r)[..., 3] > 0
        # Grow only within the cells the mask says are land or toward water:
        # distance from the shape, measured inside the tile.
        d = dist_to(shape)
        out = np.zeros((T, T, 4), np.uint8)
        lip = d <= 4.0
        out[lip] = fill[lip]
        out[(d > 3.0) & lip] = (*wet, 255)
        out[(d > 2.0) & (d <= 3.0) & (rng.random((T, T)) < 0.4)] = (*mud_dark, 255)
        row[:, mask * T:(mask + 1) * T] = out
    return row


def nearest_rgb(lab, pal):
    rgb, plab = pal
    return rgb[np.argmin(np.linalg.norm(plab - lab, axis=1))]


def hexrgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))


def material_fills(src, name):
    """A material's fill tiles. Snow and swamp water are generated, drifts
    are snow recoloured."""
    if name == "snow":
        return snow_fills()
    if name == "swamp":
        return swamp_fills()
    if name == "drift":
        return [recolour(f, DRIFT) for f in snow_fills()]
    if name == "dune":
        return material_fills(src, "sand")
    return [tile(src[s], c, r) for s, c, r in dict(MATERIALS)[name]]


def snow_fills():
    """Snow: near-white with wind ripples - short strokes in the grey that
    curl up at one end, a shadow under some. Strokes stay 1 px off the edge,
    so any two fills tile."""
    snow, ripple, shadow = hexrgb("#dfe3ed"), hexrgb("#d9d9da"), hexrgb("#bdd5de")
    rng = np.random.default_rng(23)
    out = []
    for v in range(FILLS_PER_ROW):
        f = np.zeros((T, T, 4), np.uint8)
        f[:] = (*snow, 255)
        for _ in range(rng.integers(2, 5)):
            x, y = int(rng.integers(1, T - 6)), int(rng.integers(2, T - 2))
            n = int(rng.integers(3, 6))
            f[y, x:x + n, :3] = ripple
            f[y - 1, x + n - 1, :3] = ripple
            if rng.random() < 0.5:
                f[y + 1, x + 1:x + n - 1, :3] = shadow
        out.append(f)
    return out


def stamps(a, min_px=3):
    """The 8-connected opaque blobs of `a` (at least min_px pixels), each
    cropped to its bounds - loose sprites cut from a sheet region."""
    seen = a[..., 3] == 0
    out = []
    for y0, x0 in zip(*np.nonzero(~seen)):
        if seen[y0, x0]:
            continue
        stack, px = [(y0, x0)], []
        seen[y0, x0] = True
        while stack:
            y, x = stack.pop()
            px.append((y, x))
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    ny, nx = y + dy, x + dx
                    if 0 <= ny < a.shape[0] and 0 <= nx < a.shape[1] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        stack.append((ny, nx))
        if len(px) < min_px:
            continue
        ys, xs = zip(*px)
        s = np.zeros((max(ys) - min(ys) + 1, max(xs) - min(xs) + 1, 4), np.uint8)
        for y, x in px:
            s[y - min(ys), x - min(xs)] = a[y, x]
        out.append(s)
    return out


def wetland_fills(src, base):
    """Wetland: the swamp pack's flat green (`base`) with its loose blade
    tufts (tilemap tiles 0-1 x 0-2) scattered over it, and small damp seeps -
    a dark wet line with one lit pixel - so the ground reads soggy rather than
    mown. The region's white flowers are left out: a white cross reads as a
    water glint. Stamps stay 1 px off the edge, so any two fills tile."""
    blades = [s for s in stamps(src["swamp"][0:3 * T, 0:2 * T])
              if not match(s, hexrgb("#dfe3ed")).any()]
    seep, lit = hexrgb("#48737c"), hexrgb("#91d6e8")
    rng = np.random.default_rng(29)
    out = []
    for v in range(FADE_FILLS):
        f = base.copy()
        for _ in range(rng.integers(1, 3)):
            s = blades[rng.integers(len(blades))]
            h, w = s.shape[:2]
            y, x = int(rng.integers(1, T - h)), int(rng.integers(1, T - w))
            on = s[..., 3] > 0
            f[y:y + h, x:x + w][on] = s[on]
        if v % 2 == 0:  # half the fills hold a seep
            x, y = int(rng.integers(2, T - 5)), int(rng.integers(2, T - 2))
            n = int(rng.integers(2, 4))
            f[y, x:x + n, :3] = seep
            f[y, x + n - 1, :3] = lit
        out.append(f)
    return out


# Step marks (ow_steps.png), drawn by hand: '.' clear, then palette colours
# by letter. Each pattern is centred on its 16px tile's (8, 8), which the
# renderer puts on the foot.
STEP_INK = {"d": "#829da5", "s": "#a6b7c6",   # snow print: shadow, dip
            "m": "#48737c", "w": "#4b8295",   # wetland squish: seep, water
            "h": "#91d6e8", "o": "#dfe3ed",   # lit water, a droplet's glint
            "g": "#b17a4e", "n": "#cc9770",   # sand kick: grain shadow, grain
            "l": "#f6e998",                   # a grain catching the light
            "G": "#d8b289", "N": "#e8d282"}   # the kick on scrub grass: pale sand
# A boot print, sole and heel, toe toward the way walked: one per
# FacingDirection in its enum order (down, up, left, right). The shadow sits
# under each dent's top edge (light from above), the rest a dip a step
# darker than the snow's own ripple shadows so it reads against them.
PRINTS = [
    [".d.", "...", "ddd", "sss", ".s."],   # down
    [".d.", "dsd", "sss", "...", ".d."],   # up
    [".dd.d", "sss.s", ".ss.."],           # left
    ["d.dd.", "s.sss", "..ss."],           # right
]


def faint(rows):
    """A print half filled in: shadow gone, every other pixel snowed over."""
    return ["".join("." if c == "." or (x + y) % 2 else "s" for x, c in enumerate(r))
            for y, r in enumerate(rows)]


# The squish: water wells up round the boot, throws two droplets, spreads
# into a ring and sinks back. Rows run from 5 px above the foot to 1 below.
SQUISH = [
    [".......", ".......", ".......", ".......", "..mwm..", ".mhwhm.", "..mmm.."],
    [".......", ".......", ".o...o.", ".......", ".mwwwm.", "m.hhh.m", ".mmmmm."],
    ["o.....o", ".......", ".h...h.", ".......", "mw...wm", "m.....m", ".m.m.m."],
    [".......", ".......", ".......", "h.....h", "m.....m", ".......", "..m.m.."],
]


# The sand kick: grains spray up round the boot, peak, and patter back down.
# Darker than any sand fill so they read on the beach, and on scrub grass.
# Rows run from 5 px above the foot to 1 below, like the squish.
KICK = [
    [".......", ".......", ".......", "...n...", "..g.n..", ".n.g.g.", "..ggg.."],
    [".......", "...l...", ".n...l.", "..g.n..", "g.....n", ".g.n.g.", "..g.g.."],
    ["..l....", "l....n.", ".......", "n..g..g", ".......", "g.....g", ".g...g."],
    [".......", ".......", ".......", ".n...l.", "g.....n", ".......", "g..g..g"],
]


def steps_sheet():
    """Row 0: the four PRINTS; row 1: the same half filled in; row 2: the
    SQUISH frames; row 3: the KICK frames; row 4: the kick in pale sand, for
    scrub grass, where the dark grains read as dirt. One 16px tile each."""
    a = np.zeros((5 * T, 4 * T, 4), np.uint8)

    def stamp(rows, col, row, foot_row):
        h, w = len(rows), len(rows[0])
        y0, x0 = row * T + 8 - foot_row, col * T + 8 - w // 2
        for y, line in enumerate(rows):
            for x, ch in enumerate(line):
                if ch != ".":
                    a[y0 + y, x0 + x] = (*hexrgb(STEP_INK[ch]), 255)

    for i, p in enumerate(PRINTS):
        stamp(p, i, 0, len(p) // 2)
        stamp(faint(p), i, 1, len(p) // 2)
    for i, f in enumerate(SQUISH):
        stamp(f, i, 2, 5)  # the ring's middle row on the foot
    for i, f in enumerate(KICK):
        stamp(f, i, 3, 5)
        stamp([r.replace("g", "G").replace("n", "N") for r in f], i, 4, 5)
    return a


def outline_shades(fill, pal):
    """A material's outline: its fill's mean colour, darkened twice in OKLab
    along the same hue, each snapped to the palette."""
    body = fill.reshape(-1, 4)
    mean = srgb_to_oklab(body[body[:, 3] > 0][:, :3]).mean(0)
    return [nearest_rgb(mean * [1, k, k] - [d, 0, 0], pal) for d, k in ((0.24, 1.1), (0.12, 1.05))]


def lb_body_colours(lb):
    return {tuple(px[:3]) for c, r in LB_BODY for px in tile(lb, c, r).reshape(-1, 4) if px[3]}


def outline_tile(m, fill, shades, lb_body):
    """LightBorne corner tile `m` in a material: body pixels from the fill
    (so the edge and the fill beside it are one texture), outline pixels
    dark or mid by their own lightness."""
    out = np.zeros_like(m)
    for y in range(T):
        for x in range(T):
            if m[y, x, 3] == 0:
                continue
            if tuple(m[y, x, :3]) in lb_body:
                out[y, x] = fill[y, x]
            else:
                L = srgb_to_oklab(m[y, x, :3][None].astype(np.uint8))[0, 0]
                out[y, x, :3] = shades[0] if L < 0.45 else shades[1]
                out[y, x, 3] = 255
    return out


def terrain_sheet(src, pal):
    lb = src["lb_tiles"]
    lb_body = lb_body_colours(lb)
    rows = []
    for name in TERRAIN:
        row = np.zeros((T, (FILL_COL + FILLS_PER_ROW) * T, 4), np.uint8)
        fill_tiles = material_fills(src, name)
        for i in range(FILLS_PER_ROW):
            row[:, (FILL_COL + i) * T:(FILL_COL + i + 1) * T] = fill_tiles[i % len(fill_tiles)]
        if name != "sand":
            row[:, 15 * T:16 * T] = fill_tiles[0]
            shades = outline_shades(fill_tiles[0], pal)
            for mask, (c, r) in LB_CORNERS.items():
                row[:, mask * T:(mask + 1) * T] = outline_tile(tile(lb, c, r), fill_tiles[0], shades, lb_body)
        rows.append(row)
    rows += [bank_row(lb, c) for c in BANK_STEPS]
    return np.concatenate(rows, axis=0)


def fades_sheet(src, pal):
    """One row per fade material: FADE_FILLS fills (cycled where the source
    has fewer), then its two outline colours as pixels (dark at x = 128, mid
    at x = 129). Last row: LightBorne's 16 corner shapes, each pixel coded
    by red - body, mid outline, dark outline - or clear."""
    lb = src["lb_tiles"]
    lb_body = lb_body_colours(lb)
    a = np.zeros(((len(FADES) + 1) * T, 16 * T, 4), np.uint8)
    for k, name in enumerate(FADES):
        if name == "swamp":
            continue
        fills = material_fills(src, name)
        dark, mid = outline_shades(fills[0], pal)  # off the plain fill, before any texture
        if name == "dune":
            # The beach's own sand: where dunes cover the grass's edge, the
            # outline vanishes instead of drawing a line through the sand.
            colours, counts = np.unique(fills[0].reshape(-1, 4)[:, :3], axis=0, return_counts=True)
            dark = mid = tuple(int(v) for v in colours[np.argmax(counts)])
        if name == "wetland":
            fills = wetland_fills(src, fills[0])
        for i in range(FADE_FILLS):
            a[k * T:(k + 1) * T, i * T:(i + 1) * T] = fills[i % len(fills)]
        a[k * T, 8 * T] = (*dark, 255)
        a[k * T, 8 * T + 1] = (*mid, 255)
    y0 = len(FADES) * T
    a[y0:y0 + T, 15 * T:16 * T] = (*hexrgb(MASK_BODY), 255)
    for mask, (c, r) in LB_CORNERS.items():
        m = tile(lb, c, r)
        for y in range(T):
            for x in range(T):
                if m[y, x, 3] == 0:
                    continue
                if tuple(m[y, x, :3]) in lb_body:
                    code = MASK_BODY
                else:
                    L = srgb_to_oklab(m[y, x, :3][None].astype(np.uint8))[0, 0]
                    code = MASK_DARK if L < 0.45 else MASK_MID
                a[y0 + y, mask * T + x] = (*hexrgb(code), 255)
    return a


def recolour(a, mapping):
    out = a.copy()
    for old, new in mapping.items():
        out[match(a, hexrgb(old)), :3] = hexrgb(new)
    return out


def shades_sheet(src, pal):
    lb = src["lb_tiles"]
    lb_body = lb_body_colours(lb)
    yy, xx = np.mgrid[0:T, 0:T]
    bits = (8, 4, 2, 1)
    rows = []
    for _, base, mapping in SHADES:
        fills = [recolour(f, mapping) for f in material_fills(src, base)]
        outline = outline_shades(fills[0], pal)
        row = np.zeros((T, (81 + FILLS_PER_ROW) * T, 4), np.uint8)
        for i in range(FILLS_PER_ROW):
            row[:, (81 + i) * T:(82 + i) * T] = fills[i % len(fills)]
        for idx in range(81):
            st = [idx // 27 % 3, idx // 9 % 3, idx // 3 % 3, idx % 3]  # TL TR BL BR
            on_base = sum(b for b, q in zip(bits, st) if q >= 1)
            shaded = sum(b for b, q in zip(bits, st) if q == 2)
            plain = sum(b for b, q in zip(bits, st) if q == 1)
            if shaded == 0:
                continue
            if on_base == 15:
                t = fills[0].copy()
            else:
                c, r = LB_CORNERS[on_base]
                t = outline_tile(tile(lb, c, r), fills[0], outline, lb_body)
            # Dither out toward plain base only, inward from LightBorne's
            # wobbly shape (not the straight cell line, which reads as a
            # staircase); against anything else the base's outline is the edge.
            if plain:
                c, r = LB_CORNERS[15 - plain]
                d = dist_to(tile(lb, c, r)[..., 3] == 0)
            else:
                d = np.full((T, T), 99.0)
            keep = BAYER4[yy % 4, xx % 4] < np.clip(d / SHADE_BAND, 0, 1) * 16
            t[~keep] = 0
            row[:, idx * T:(idx + 1) * T] = t
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
        if name in PROP_RECOLOUR:
            a = recolour(a, PROP_RECOLOUR[name])
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

    # The wet bottom rows: two strip rows per shelf (frames on a shelf never
    # overlap horizontally), foam then sunk, at the frame's own columns.
    shelf_tops = sorted({r for _, r, _, _ in place.values()})
    wet = np.zeros((2 * len(shelf_tops) * T, ATLAS_TILES * T, 4), np.uint8)
    yy, xx = np.mgrid[0:T, 0:T]
    # Sunk: fully kept down to row 6, then thinning to almost nothing at the
    # base. Ordered dither, so it stays crisp.
    sunk = BAYER4[yy % 4, xx % 4] < np.clip((T - 1 - yy) / 9.0, 0, 1) * 16
    # Foam: the water risen to WATERLINE - a light line where the roots cut
    # it, one px wider each side, and half the root showing below.
    half = (yy <= WATERLINE) | (BAYER4[yy % 4, xx % 4] < 8)
    foam_rgb = hexrgb("#6ea7c6")  # the water's own ripple colour
    frames = []
    for i in range(len(sprites)):
        c, r, fw, fh = place[i]
        k = shelf_tops.index(r)
        if sprites[i][0] not in LAPPING:
            frames.append((c, r, fw, fh, -1))
            continue
        bottom = sprites[i][1][(fh - 1) * T:fh * T]
        foam = bottom.copy()
        foam[~np.tile(half, (1, fw))] = 0
        line = np.pad(bottom[WATERLINE, :, 3] > 0, 1)
        foam[WATERLINE, line[:-2] | line[1:-1] | line[2:]] = (*foam_rgb, 255)
        down = bottom.copy()
        down[~np.tile(sunk, (1, fw))] = 0
        wet[2 * k * T:(2 * k + 1) * T, c * T:(c + fw) * T] = foam
        wet[(2 * k + 1) * T:(2 * k + 2) * T, c * T:(c + fw) * T] = down
        frames.append((c, r, fw, fh, 2 * k))
    return atlas, wet, frames


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
           "// wetRow: the row of ow_props_wet.png holding the frame's bottom row",
           "// with a foam line at the waterline; wetRow + 1 holds it sunk. Same",
           "// columns as the frame. -1: the prop's roots do not lap.",
           "struct Frame {", "  int col, row, w, h, wetRow;", "};", "",
           "enum Id : int {"]
    out += [f"  {n}," for n in names]
    out += ["  COUNT", "};", "",
            f"// The tallest frame, in tiles: how far below the screen a prop can be",
            f"// rooted and still reach into view.",
            f"inline constexpr int kTallestTiles = {tallest};", "",
            "inline constexpr Frame kFrames[COUNT] = {"]
    out += [f"    {{{c}, {r}, {w}, {h}, {k}}}, // {n}" for n, (c, r, w, h, k) in zip(names, frames)]
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
    # The swamp blends were mixed for the generated swamp water; kept out of
    # the packs' quantization so no tree pixel snaps to them.
    keep = np.array([tuple(c) not in {hexrgb(h) for h in SWAMP_BLENDS} for c in pal[0]])
    src = load_sources(a.packs, (pal[0][keep], pal[1][keep]))
    src["gen"] = generated_decals(src)
    if a.dump:
        os.makedirs(a.dump, exist_ok=True)
        for name, px in src.items():
            Image.fromarray(px, "RGBA").save(os.path.join(a.dump, name + ".png"))

    assets = os.path.join(ROOT, "assets")
    save(water_sheet(), os.path.join(assets, "ow_water.png"))
    save(river_sheet(), os.path.join(assets, "ow_river.png"))
    save(swamp_water_sheet(), os.path.join(assets, "ow_swamp_water.png"))
    save(glints_sheet(), os.path.join(assets, "ow_glints.png"))
    save(steps_sheet(), os.path.join(assets, "ow_steps.png"))
    save(src["coast"], os.path.join(assets, "ow_coast.png"))
    save(terrain_sheet(src, pal), os.path.join(assets, "ow_terrain.png"))
    save(shades_sheet(src, pal), os.path.join(assets, "ow_shades.png"))
    save(fades_sheet(src, pal), os.path.join(assets, "ow_fades.png"))
    atlas, wet, frames = props_atlas(src)
    save(atlas, os.path.join(assets, "ow_props.png"))
    save(wet, os.path.join(assets, "ow_props_wet.png"))
    write_header(frames, os.path.join(ROOT, "src", "render", "overworld_sprites.hpp"))
    print(f"wrote src/render/overworld_sprites.hpp: {len(frames)} props")
    return 0


if __name__ == "__main__":
    sys.exit(main())
