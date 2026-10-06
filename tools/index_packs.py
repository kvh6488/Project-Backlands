#!/usr/bin/env python3
"""Index the local asset packs, then search the index by description.

Usage:
    python tools/index_packs.py                       # (re)build the manifest
    python tools/index_packs.py --search "toxic barrel"
    python tools/index_packs.py --search "quill pen" --top 5
    python tools/index_packs.py --packs "../Asset packs" --out artifacts/pack_manifest.json

Why: /generate-asset must search before it generates. The PixelLab trial is
40 generations with no credit fallback, and ../Asset packs already holds ~500
sheets covering almost every underworld prop the game needs. A search that
misses a sprite that exists costs a generation that cannot be bought back, so
this index errs towards recall: every hit is reported with its sheet, size
and inferred grid, and the caller looks at the sheet before deciding.

How: one pass over every PNG (aseprite and __MACOSX skipped) records size,
an inferred cell size, how many cells hold pixels, the dominant colours, and
how much of the sheet already sits on the master palette. Filenames are the
main search key, but a multi-object sheet like Furniture.png names none of
its sprites, so SHEET_CONTENTS below lists what those sheets hold by eye.
Search is token overlap between the query and each entry's tags with a small
synonym table - no fuzzy matching, so a miss is a real miss, not a near one.

Cell inference: for each candidate size that divides both dimensions, score
how many grid lines fall in transparent seams; the smallest candidate with a
clean-seam majority wins. It is a heuristic and is labelled as such in the
output - confirm against the pack's own Info/ReadMe before cutting.

The manifest is a cache in artifacts/ (gitignored): it describes a folder
outside the repo, so it is rebuilt wherever it is missing rather than
committed. Requires numpy and Pillow.
"""
import argparse
import json
import os
import re
import sys

import numpy as np
from PIL import Image

from quantize import srgb_to_oklab

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_PACKS = os.path.join(ROOT, "..", "Asset packs")
DEFAULT_OUT = os.path.join(ROOT, "artifacts", "pack_manifest.json")
DEFAULT_PALETTE = os.path.join(ROOT, "assets", "palette.json")

CELL_CANDIDATES = (16, 24, 32, 48, 64)  # nothing in the packs is authored below 16
SEAM_THRESHOLD = 0.6  # fraction of grid lines that must be clean seams

# What the multi-object sheets hold, keyed by a path suffix. Written by eye;
# extend it when a search misses something you can see on a sheet.
SHEET_CONTENTS = {
    "PostApoc_Workshop/PostApoc_Workshop.png": (
        "workbench table desk drawer stool locker cabinet cupboard shelf "
        "shelving rack toolbox chest tool tools wrench hammer saw screwdriver "
        "barrel drum jerrycan can canister bottle jar bucket tyre tire gas "
        "cylinder tank fire extinguisher pegboard plank board sheet metal "
        "corrugated pipe wire cable window crate box sign danger warning "
        "poster helmet welding mask paper pencil wall room floor concrete"
    ),
    "PostApoc_Workshop/PostApoc_Workshop_WithShadow.png": "same as PostApoc_Workshop.png with drop shadows",
    "PostApoc_Workshop/PostApoc_Workshop_Icons.png": (
        "inventory icon icons tool tools wrench hammer saw nail screw bolt "
        "nut wood plank paper scrap metal wire bottle jar can food"
    ),
    "PostApoc_Workshop/PostApoc_Workshop_RoomTiles.png": "wall door floor room tile tiles",
    "Environment/Props/Static/Furniture.png": (
        "furniture bed chair table bench stool bookshelf bookcase shelf "
        "wardrobe cabinet cupboard dresser drawer counter door window "
        "arch doorway ladder stairs staircase fence railing barrel keg "
        "pot vase plant lantern lamp rug carpet chandelier"
    ),
    "Environment/Props/Static/Dungeon_Props.png": (
        "dungeon cannon torch candle sconce brazier banner flag door gate "
        "spike spikes altar pedestal"
    ),
    "Environment/Props/Static/Tools.png": (
        "tool tools axe pickaxe hammer shovel hoe scythe sickle sword knife "
        "dagger spear pitchfork saw anvil grindstone wheel bucket barrel "
        "whetstone chisel pliers tongs scissors shears nail bolt"
    ),
    "Environment/Props/Static/Esoteric.png": (
        "potion flask vial bottle alchemy magic cauldron urn jar candle "
        "book tome scroll paper quill pen feather pencil ink card mirror "
        "crystal gem rune chest trunk"
    ),
    "Environment/Props/Static/Resources.png": "resource resources log wood stone ore ingot bar coal gem crystal",
    "Environment/Props/Static/Rocks.png": "rock rocks boulder stone",
    "Environment/Props/Static/Vegetation.png": "grass bush shrub flower plant weed vegetation mushroom fungus",
    "Environment/Props/Static/Meat.png": "meat food steak ham fish",
    "Environment/Props/Static/Farm.png": "farm crop crops hay bale scarecrow fence trough",
    "Environment/Structures/Buildings/Interior/Interior_Props_01.png": (
        "interior bed chair table stool bench desk counter bar shelf "
        "bookshelf cabinet cupboard wardrobe chest crate barrel keg fireplace "
        "hearth oven stove furnace chimney door window curtain rug carpet "
        "painting picture frame clock candle lantern lamp chandelier plant "
        "pot bowl plate cup mug bottle jar food cheese bread fruit vegetable "
        "pumpkin cabbage carrot mushroom skull cobweb spider web"
    ),
    "Environment/Structures/Buildings/Props.png": (
        "door doorway arch window shutter shutters awning sign signpost "
        "bench planter fence gate fountain pillar column lamp post"
    ),
    "Environment/Tilesets/Dungeon_Tiles.png": "dungeon floor wall tileset tiles stone brick",
    "Environment/Tilesets/Floors_Tiles.png": "floor tileset tiles grass dirt path",
    "Environment/Tilesets/Wall_Tiles.png": "wall tileset tiles",
    "Environment/Tilesets/Water_tiles.png": "water tileset tiles pond river",
    "Backrooms Lvl1 {Tileset}/BCKRMlv1_Prop_set.png": (
        "backrooms prop props door cupboard cabinet locker desk table tv "
        "television chair ladder hazard tape caution sign run graffiti "
        "ceiling light fluorescent lamp"
    ),
    "Backrooms Lvl1 {Tileset}/BCKRMlv1_Floor_set.png": "backrooms floor carpet tileset tiles",
    "Backrooms Lvl1 {Tileset}/BCKRMlv1_Wall_set.png": "backrooms wall wallpaper tileset tiles",
    "Backrooms Lvl1 {Tileset}/BCKRMlvl1_Elevator_set.png": "backrooms elevator lift door",
    "Backrooms Items/Backrooms_Items_Sheet.png": (
        "almond water bar bluebook book keycard bugspray camera crowbar "
        "ducttape tape flashlight torch fuse gasmask mask note paper pills "
        "rope screwdriver walkie talkie radio thermometer wheel"
    ),
    "BigWander_TheRitual/Spritesheet/Spritesheet_TheDarkRitual_BigWander.png": (
        "ritual occult amulet bones skull book tome candle card tarot "
        "container goblet crystal sphere ball dagger knife horseshoe lizard "
        "mirror pentagram rune voodoo doll magic"
    ),
    "sci-fi-facility-asset-pack/tileset.png": "scifi facility floor wall tileset tiles metal",
    "sci-fi-facility-asset-pack/doodads_spritesheet.png": "scifi doodad doodads radiation sign vent panel terminal",
    "sci-fi-facility-asset-pack/crates_spritesheet.png": "scifi crate crates box boxes",
    "sci-fi-facility-asset-pack/computer_spritesheet.png": "scifi computer terminal screen console",
    "sci-fi-facility-asset-pack/inspector_spritesheet.png": "player inspector character walk animation",
    "mushrooms/mushrooms_pixel_asset.png": "mushroom mushrooms fungus toadstool",
}

# Query-side synonyms: each key expands to itself plus its list.
SYNONYMS = {
    "barrel": ["drum", "keg", "canister"],
    "locker": ["cabinet", "cupboard", "wardrobe"],
    "cupboard": ["cabinet", "locker", "wardrobe"],
    "pencil": ["pen", "quill"],
    "pen": ["quill", "pencil"],
    "table": ["desk", "workbench", "bench"],
    "torch": ["flashlight", "lantern", "lamp"],
    "book": ["tome", "bluebook"],
    "box": ["crate", "chest"],
    "sign": ["poster", "warning", "danger"],
    "monster": ["mob", "minion", "boss", "enemy"],
    "enemy": ["mob", "minion", "boss", "monster"],
    "mob": ["minion", "boss", "monster"],
    "player": ["character", "inspector"],
}


def tokenize(text):
    return [t for t in re.split(r"[^a-z0-9]+", text.lower()) if t]


def load_palette_lab(path):
    with open(path) as f:
        doc = json.load(f)
    rgb = np.array([c["rgb"] for r in doc["ramps"] for c in r["colours"]], dtype=np.uint8)
    return srgb_to_oklab(rgb)


def infer_cell(alpha):
    """Smallest candidate whose grid lines mostly fall in transparent seams.

    Returns (cell, seamScore, source). A gapless atlas has no seams to read,
    so it is *assumed* 16 - the convention every pack here states - and
    labelled so the caller knows it was not measured.
    """
    h, w = alpha.shape
    if w <= 64 and h <= 64:
        return min(w, h), 1.0, "single"  # a single sprite: the sheet is the cell (occupiedCells stays 0)
    col_cover = (alpha > 0).mean(axis=0)
    row_cover = (alpha > 0).mean(axis=1)
    best, best_score = None, -1.0
    for c in CELL_CANDIDATES:
        if w % c or h % c:
            continue
        lines = []
        for x in range(c, w, c):
            lines.append(max(col_cover[x - 1], col_cover[x]) < 0.05)
        for y in range(c, h, c):
            lines.append(max(row_cover[y - 1], row_cover[y]) < 0.05)
        score = float(np.mean(lines)) if lines else 1.0
        if score >= SEAM_THRESHOLD:
            return c, score, "seams"
        if score > best_score:
            best, best_score = c, score
    if best_score < 0.1 and w % 16 == 0 and h % 16 == 0:
        return 16, best_score, "assumed"
    return best, best_score, "best-guess"


def describe(path, rel, palette_lab):
    im = Image.open(path).convert("RGBA")
    px = np.asarray(im)
    alpha = px[..., 3]
    opaque = px[alpha > 0][:, :3]
    cell, seam, source = infer_cell(alpha)
    entry = {
        "path": rel,
        "pack": rel.split("/")[0],
        "width": im.width,
        "height": im.height,
        "cell": cell,
        "cellSource": source,
        "cellSeamScore": round(seam, 2),
        "occupiedCells": 0,
        "colours": 0,
        "dominant": [],
        "paletteFit": None,
    }
    if cell and im.width % cell == 0 and im.height % cell == 0:
        cw, ch = im.width // cell, im.height // cell
        blocks = (alpha > 0).reshape(ch, cell, cw, cell).any(axis=(1, 3))
        entry["occupiedCells"] = int(blocks.sum())
    if len(opaque):
        cols, counts = np.unique(opaque, axis=0, return_counts=True)
        order = np.argsort(-counts)
        entry["colours"] = int(len(cols))
        entry["dominant"] = ["#%02x%02x%02x" % tuple(cols[i]) for i in order[:5]]
        # Fraction of opaque pixels within a just-noticeable step of a
        # palette colour: 1.0 means the sheet needs no quantizing.
        lab = srgb_to_oklab(cols)
        d = np.sqrt(((lab[:, None, :] - palette_lab[None, :, :]) ** 2).sum(-1)).min(axis=1)
        entry["paletteFit"] = round(float((counts * (d < 0.02)).sum() / counts.sum()), 3)
    tags = set(tokenize(rel))
    for suffix, words in SHEET_CONTENTS.items():
        if rel.endswith(suffix):
            tags.update(tokenize(words))
    entry["tags"] = sorted(tags)
    return entry


def build(packs, out, palette):
    palette_lab = load_palette_lab(palette)
    entries = []
    for dirpath, dirnames, filenames in os.walk(packs):
        dirnames[:] = [d for d in dirnames if d != "__MACOSX"]
        for name in sorted(filenames):
            if not name.lower().endswith(".png"):
                continue
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, packs).replace(os.sep, "/")
            try:
                entries.append(describe(full, rel, palette_lab))
            except Exception as e:  # a corrupt PNG must not sink the index
                print(f"skip {rel}: {e}", file=sys.stderr)
    entries.sort(key=lambda e: e["path"])
    manifest = {"packsDir": os.path.abspath(packs), "count": len(entries), "entries": entries}
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        json.dump(manifest, f, indent=1)
    return manifest


def expand_query(query):
    terms = {}
    for t in tokenize(query):
        terms[t] = 2.0
        for s in SYNONYMS.get(t, []):
            terms.setdefault(s, 1.0)
    return terms


def search(manifest, query, top):
    terms = expand_query(query)
    scored = []
    for e in manifest["entries"]:
        score, hits = 0.0, []
        for term, weight in terms.items():
            if term in e["tags"]:
                score += weight
                hits.append(term)
            elif len(term) >= 4 and any(term in tag for tag in e["tags"]):
                score += weight * 0.5
                hits.append(term + "~")
        if score > 0:
            scored.append((score, e, hits))
    scored.sort(key=lambda s: (-s[0], s[1]["path"]))
    return scored[:top]


def print_results(results, query):
    if not results:
        print(f'no pack sheet matches "{query}"')
        return
    print(f'"{query}" - top {len(results)} of the packs:')
    for score, e, hits in results:
        fit = "n/a" if e["paletteFit"] is None else f"{e['paletteFit']:.0%}"
        print(f"  [{score:4.1f}] {e['path']}")
        print(
            f"         {e['width']}x{e['height']}  cell {e['cell']} ({e['cellSource']})  "
            f"{e['occupiedCells']} occupied cells  {e['colours']} colours  "
            f"palette fit {fit}  matched: {' '.join(hits)}"
        )


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--packs", default=DEFAULT_PACKS)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--palette", default=DEFAULT_PALETTE)
    ap.add_argument("--search", metavar="QUERY")
    ap.add_argument("--top", type=int, default=8)
    ap.add_argument("--rebuild", action="store_true", help="rebuild even if the manifest exists")
    args = ap.parse_args()

    if args.search and os.path.exists(args.out) and not args.rebuild:
        with open(args.out) as f:
            manifest = json.load(f)
    else:
        if not os.path.isdir(args.packs):
            print(f"packs directory not found: {args.packs}", file=sys.stderr)
            return 1
        manifest = build(args.packs, args.out, args.palette)
        print(f"indexed {manifest['count']} sheets from {manifest['packsDir']} -> {args.out}")

    if args.search:
        print_results(search(manifest, args.search, args.top), args.search)
    return 0


if __name__ == "__main__":
    sys.exit(main())
