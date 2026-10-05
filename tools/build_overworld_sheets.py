#!/usr/bin/env python3
"""Build the overworld sheets in assets/ (ow_*.png) from the asset packs.

Usage:
    python tools/build_overworld_sheets.py [--packs "../Asset packs"] [--out assets]

Why: the surface art comes from four packs (Pixel Crawler, High Tides,
LightBorne, Tiny Wonder Swamp), and only chosen regions of them ship. This is
the one place that says which regions, and every edit made to them before they
meet the palette, so a palette change re-runs this script instead of anyone
touching a sheet by hand (docs/palette.md, "Overworld additions").

How, per sheet:
  1. Crop the chosen region of the pack file (SHEETS).
  2. CUT_OUT: make High Tides' flat sea transparent, so its coast tiles sit on
     our own water instead of carrying a second, brighter blue.
  3. RECOLOUR: swap exact source colours before quantizing - the art is
     edited, not the palette. Pixel Crawler's red pine becomes rust orange,
     which let the palette drop its autumn red.
  4. Quantize to assets/palette.json (tools/quantize.py: nearest in OKLab).
The water fill is generated, not cropped: see water_sheet().

Every tree variant of every pack is kept, seasons included; which biome and
season draws which is the renderer's decision, not this script's.

Requires numpy and Pillow. Deterministic.
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image

from quantize import load_palette, quantize

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PC = "Pixel Crawler - Free Pack 2.11/Pixel Crawler - Free Pack/Environment"
HT = "High Tides - Premium Pack"
LB = "LightBorne 1.1v"
SW = "Tiny Wonder Swamp"
TREES = PC + "/Props/Static/Trees"

# output name -> (pack-relative source, crop box or None for the whole file)
SHEETS = {
    "ow_coast":        (HT + "/Tilesets/Land_to_Sea Transitions.png", (0, 0, 400, 208)),
    "ow_beach":        (HT + "/Tilesets/Base_Tiles.png", (0, 0, 64, 48)),
    "ow_sand_grass":   (HT + "/Tilesets/Land_Transitions.png", (0, 0, 144, 72)),
    "ow_floors_pc":    (PC + "/Tilesets/Floors_Tiles.png", (0, 0, 160, 416)),  # grass, gravel, snow + edges
    "ow_forest_floor": (LB + "/Tilesets/Exterior/tiles.png", (0, 0, 528, 448)),
    "ow_wetland":      (SW + "/swamp tilemap.png", (0, 0, 144, 160)),
    "ow_tree_lb_oak":  (LB + "/Environment/Vegetation/Trees/oak tree.png", None),
    "ow_tree_lb_birch": (LB + "/Environment/Vegetation/Trees/Birch tree.png", None),
    "ow_tree_lb_fir":  (LB + "/Environment/Vegetation/Trees/fir tree.png", None),
    "ow_log_lb":       (LB + "/Environment/Deco/fallen tree.png", None),
    "ow_tree_willow":  (SW + "/objects&items/swamp objects.png", (96, 0, 288, 208)),
    "ow_tree_palm":    (HT + "/Objects/Foliage.png", None),
    "ow_bush_lb":      (LB + "/Environment/Vegetation/bushes.png", None),
    "ow_plants_pc":    (PC + "/Props/Static/Vegetation.png", (0, 0, 400, 320)),
    "ow_rock_lb":      (LB + "/Environment/Deco/rocks.png", None),
    "ow_rock_pc":      (PC + "/Props/Static/Rocks.png", (0, 16, 208, 304)),  # drops the "PALETTE:" label
    "ow_reeds":        (SW + "/objects&items/swamp water objects.png", None),
}
for model in (1, 2, 3):
    for size in (2, 3, 4, 5):
        SHEETS[f"ow_tree_pc{model}_size{size}"] = (f"{TREES}/Model_0{model}/Size_0{size}.png", None)

CUT_OUT = {"ow_coast": [(0x1D, 0xAF, 0xD9)]}
RECOLOUR = {f"ow_tree_pc3_size{s}": {(0x84, 0x20, 0x20): (0xAD, 0x52, 0x26)} for s in (2, 3, 4, 5)}


def match(a, rgb):
    return (a[..., 0] == rgb[0]) & (a[..., 1] == rgb[1]) & (a[..., 2] == rgb[2])


def water_sheet():
    """8 seamless 16px fills, five plain and three with one short light dash -
    Pixel Crawler's open-water ripples as they look on the palette. The
    renderer picks a variant per tile by hash, so dashes land irregularly.
    Dashes stay off the tile edge, so any two variants tile seamlessly."""
    base, light = (0x4E, 0x91, 0xAF, 255), (0x6E, 0xA7, 0xC6, 255)
    dashes = {5: (3, 4, 3), 6: (9, 10, 2), 7: (5, 12, 4)}  # variant: (x, y, length)
    a = np.zeros((16, 8 * 16, 4), np.uint8)
    a[:] = base
    for v, (x, y, n) in dashes.items():
        a[y, v * 16 + x:v * 16 + x + n] = light
    return Image.fromarray(a, "RGBA")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--packs", default=os.path.join(os.path.dirname(ROOT), "Asset packs"))
    ap.add_argument("--out", default=os.path.join(ROOT, "assets"))
    ap.add_argument("--palette", default=os.path.join(ROOT, "assets", "palette.json"))
    a = ap.parse_args()
    pal_rgb, pal_lab = load_palette(a.palette)

    def emit(img, name):
        quantize(img, pal_rgb, pal_lab).save(os.path.join(a.out, name + ".png"))
        print(f"wrote {name}.png {img.width}x{img.height}")

    for name, (src, box) in SHEETS.items():
        img = Image.open(os.path.join(a.packs, src)).convert("RGBA")
        if box:
            img = img.crop(box)
        px = np.array(img)
        for rgb in CUT_OUT.get(name, []):
            px[match(px, rgb), 3] = 0
        for old, new in RECOLOUR.get(name, {}).items():
            px[match(px, old), :3] = new
        emit(Image.fromarray(px, "RGBA"), name)
    emit(water_sheet(), "ow_water")
    return 0


if __name__ == "__main__":
    sys.exit(main())
