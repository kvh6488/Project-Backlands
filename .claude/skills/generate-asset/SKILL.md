---
name: generate-asset
description: Source a sprite for Project Backlands - search the local asset packs first, and only on a genuine miss generate one with PixelLab at 16px on the master palette. Use when a feature needs art that is not yet in assets/.
---

# /generate-asset <sprite description>

Find or make a 16px-per-cell, palette-conformant sprite for `$ARGUMENTS` and
put it in `assets/`. **Search is the default outcome; generation is the
exception.** The PixelLab trial is 40 generations with no credit fallback,
and a wasted one cannot be bought back.

## 1. Search the packs (no budget, always first)

```bash
python tools/index_packs.py --search "$ARGUMENTS"
```

Builds `artifacts/pack_manifest.json` from `../Asset packs` on first use
(~4 s), then ranks sheets by tag overlap. Every hit shows size, inferred cell
size and whether it was measured (`seams`), a single sprite, or `assumed`
16 because the sheet is a gapless atlas.

Then **look**: open the top sheets with the Read tool (composite onto a dark
background first if it is mostly transparent) and find the sprite. Multi-
object sheets are tagged by hand in `SHEET_CONTENTS` inside
`tools/index_packs.py`; if you can see something the search could not name,
add the word there so the next search finds it.

Report the best matches, with the sheet and the tile position of the sprite.
**If one fits, stop here** — cut it in step 4. Try two rewordings before you
call it a miss; the packs hold ~480 sheets and almost every underworld prop.

Known routes for common asks:

- Furniture, tools, barrels, lockers, signs → `PostApoc_Workshop/` (already in
  `assets/PostApoc_Workshop_16px.png`, halved — check whether the object is
  on it before cutting a new sheet).
- Backrooms hand items (almond water, keycards, flashlight, note) →
  `Backrooms Items/sprites/*.png`, native 16×16, one file each.
- Occult props, books, candles, cards → `BigWander_TheRitual/IndividualSprites/`.
- Household interiors, potions, quill/pen → Pixel Crawler `Furniture.png`,
  `Interior_Props_01.png`, `Esoteric.png`.
- Mobs → `Mobs/` is 32-based and must go through `tools/downsample_sheet.py`
  before it can enter `assets/`.

## 2. Check the budget before generating

Only on a genuine miss. Call `mcp__pixellab__get_balance` and report
`generations_remaining`. **Do not generate if it is below 20** — that half is
the user's own testing budget — unless the user says so in this
conversation. State what the search found and why none of it fits, so the
generation is a decision, not a reflex.

## 3. Generate at world density

One generation is the plan; a second is a retry the user has agreed to.

- Props: `mcp__pixellab__create_map_object` or
  `mcp__pixellab__create_image_pixflux` with `no_background: true`. Canvas is
  the sprite's cell footprint at **16 px per cell** — a 1×2 prop is 16×32 —
  but the API floor is 32×32 total area, so ask for 32×32 / 32×64 and expect
  to crop to the content bbox. Do not ask for a bigger canvas and shrink it:
  PixelLab draws denser art on a bigger canvas and a downsample turns it to
  mush (the Phase 5 barrel badge, two generations lost that way).
- Characters: `mcp__pixellab__create_character` at the same density.
- Force the palette: base64 `assets/palette_strip.png` (56×1, one texel per
  colour) as `color_image_base64` (pixflux) or via
  `mcp__pixellab__reduce_colors` with `palette_image_base64` afterwards.
  Pass an existing sheet from `assets/` as the style reference when the tool
  accepts one.
- A variation of an existing sprite is an img2img pass, not a fresh prompt:
  `init_image_base64` with `init_image_strength` around 300 keeps the pack's
  silhouette (this is how the mushrooms were made).
- Poll with the matching `get_*` tool, download the PNG **outside** `assets/`
  (`../Asset packs/generated/<name>.png`) so a palette change can re-quantize
  from the original.

## 4. Quantize and verify

Sources stay outside `assets/`; only the quantized copy goes in.

```bash
python tools/quantize.py <source.png> assets/<name>.png
python tools/quantize.py --check assets/
```

`--check` must report 100 % — a stray colour is a defect. Then verify the
grid with the Read tool and a quick measurement: dimensions are multiples
of 16, and the content bbox of each sprite sits inside its tile(s). A hand-
measured off-grid rectangle is a bug; draw sites address sheets with
`grid::srcTile(col, row, w, h)` and derive destinations with `grid::destFor`
/ `grid::standingOn` (`src/core/grid.hpp`), never with a scale of their own.

For a 32-based source, halve it first:

```bash
python tools/downsample_sheet.py <source.png> <halved.png>
```

then quantize the halved copy.

## 5. Reconfigure, then wire it in

Assets are copied to `build/assets/` at **configure** time only:

```bash
cmake --preset mingw-debug
```

A plain build will not see the new file. Load it through
`assets::loadTexture` in the renderer that owns it (`src/render/`), add the
`ItemDefinition` to `ItemDatabase` and a spawn rule to `ItemSpawner` if it is
a placed item, and prove it draws with `/headless-test` — a presentation
scenario with a fixture seed and a single `checkpoint`, like
`scenarios/furniture.txt`.

## 6. Report

Where the sprite came from (sheet + tile, or generation id and the prompt),
generations remaining, the `--check` result, and the scenario that shows it.
